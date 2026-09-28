// Jarvis-HOOK offline export. Uses the user's existing Editor compiler/validator.
// Every output path is explicit; this tool never writes the Editor or game inputs.
using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using FFXProjectEditor.FfxLib.Ai;
using FFXProjectEditor.FfxLib.Ai.Sin;

internal static class Program
{
    private static string Required(string[] args,string name)
    {
        int index=Array.IndexOf(args,name);
        return index>=0 && index+1<args.Length ? Path.GetFullPath(args[index+1]) : throw new ArgumentException($"Required: {name}");
    }
    public static int Main(string[] args)
    {
        try
        {
            string editor=Required(args,"--editor"),source=Required(args,"--source"),output=Required(args,"--output");
            string editorDirectory=Path.GetDirectoryName(editor)!;
            AssemblyLoadContext.Default.Resolving+=(_,name)=>
            {
                string file=Path.Combine(editorDirectory,name.Name+".dll");
                return File.Exists(file)?AssemblyLoadContext.Default.LoadFromAssemblyPath(file):null;
            };
            Directory.CreateDirectory(output);
            return Export(source,output,editor);
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
    private static int U32(byte[] data,int offset)=>checked((int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset,4)));
    private static int U16(byte[] data,int offset)=>BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset,2));
    private static ulong Hash(byte[] bytes)
    {
        ulong value=14695981039346656037UL;
        foreach(byte b in bytes)value=unchecked((value^b)*1099511628211UL);
        return value;
    }
    private static AiInstruction Op(byte code,ushort operand=0)=>new()
    {
        Offset=-1,Opcode=code,HasOperand=AiScript_File.IsOperandBearing(code),Operand=operand,OperandKind=AiScript_File.OperandKindOf(code)
    };
    private static byte[] Ai(byte[] mon)=>AiScript_File.SliceAiFileFromMonster(mon)??throw new InvalidDataException("Missing AI partition");
    private static int InitEntry(byte[] mon,int worker)
    {
        int start=U32(mon,8),limit=U32(mon,12),count=mon[start],pre=mon[start+1];
        int records=start+pre+((pre&1)==0?2:3);
        for(int n=0;n<count;++n)
        {
            int record=records+4*n;
            if(record+4>limit)throw new InvalidDataException("Worker record exceeds the file");
            if(mon[record]!=worker || mon[record+1]!=2)continue;
            int section=start+U16(mon,record+2);
            if(section+4>limit || U16(mon,section)<1)throw new InvalidDataException("Missing combat init event");
            // WorkerFile purpose zero is onTurn, not init. Generic ATEL worker
            // initialization uses entry zero (the existing Editor opener path).
            return 0;
        }
        throw new InvalidDataException("No matching combat worker");
    }
    private static byte[] Require(SinMonsterEmitResult result)
    {
        if(!result.Ok || result.EditedMonster is null)throw new InvalidDataException(result.Error??"Emitter rejected the profile");
        if(result.WorkerResolution?.Contains("STRUCTURAL",StringComparison.OrdinalIgnoreCase)==true)throw new InvalidDataException("Unproven worker fallback");
        return result.EditedMonster;
    }
    private static byte[] Once(byte[] mon,int worker,int entry,IReadOnlyList<AiInstruction> body,string id)
    {
        byte[] ai=Ai(mon);var script=AiScript_File.Read(ai);
        int init=InitEntry(mon,worker);
        if(init==entry || init>=script.Workers[worker].Entrypoints.Count)throw new InvalidDataException("Init/event alias cannot own a one-shot latch");
        int occupied=script.Variables.Where(v=>v.Storage==0x56).Select(v=>v.Slot+4).DefaultIfEmpty(0).Max();
        int slot=(Math.Max(occupied,script.Workers.Max(w=>w.PrivateDataLength))+3)&~3;
        if(slot>1020 || script.Variables.Count>=ushort.MaxValue)throw new InvalidDataException("Private latch exceeds the bounded profile");
        int descriptor=script.Workers[worker].DescriptorOffset;
        // Native allocator 86C050 sums Align16(desc+10); this explicit extension
        // reserves new storage instead of stealing the monster's existing vars.
        BinaryPrimitives.WriteUInt32LittleEndian(ai.AsSpan(descriptor+0x10,4),checked((uint)(slot+4)));
        script=AiScript_File.Read(ai);ushort variable=checked((ushort)script.Variables.Count);
        ai=AiScript_File.AppendPrivateVariableDescriptor(script,slot);
        mon=AiScript_File.SpliceAiFileIntoMonsterGrow(mon,ai);
        mon=Require(SinSandboxApplySession.TryEmitRawAction(mon,id+"-init",new[]{Op(0xAE,0),Op(0xA0,variable)},entrypointOverride:init));
        var guardedBody=new List<AiInstruction>{Op(0xAE,1),Op(0xA0,variable)};
        guardedBody.AddRange(body);
        return Require(SinSandboxApplySession.TryEmitRawAction(mon,id,guardedBody,entrypointOverride:entry,
            guardOverride:new[]{Op(0x9F,variable),Op(0xAE,0),Op(0x06)}));
    }
    private static byte[] Emit(byte[] mon,int curse)
    {
        string id=$"UNI-{curse:D3}";var script=AiScript_File.Read(Ai(mon));
        if(!AiWorkerMapping.TryResolveCombatOnTurn(mon,script,out var turn,out string error))throw new InvalidDataException(error);
        if(curse==1)
        {
            var plan=SinDryRunPlanner.Plan(SinPilotRecipes.TurnOneSelfBuff());
            return Once(mon,turn.WorkerIndex,turn.EntrypointIndex,plan.Steps.Single().BodyOps,id);
        }
        if(curse==2)
        {
            if(!AiWorkerMapping.TryResolveCombatOnHit(mon,script,out var hit,out error))throw new InvalidDataException(error);
            var snippet=AiSnippetLibrary.ById("guard-counterattack-cmd")??throw new InvalidDataException("Missing canonical counter template");
            // Match the Editor's explicit Counter March bake: native Delay
            // Attack, not the later experimental custom animation/caster row.
            var (guard,counterBody)=snippet.ExpandGuarded(new AiSnippetArgs(0x3006,0,0xFFEF));
            return Require(SinSandboxApplySession.TryEmitRawAction(mon,id,counterBody,entrypointOverride:hit.EntrypointIndex,guardOverride:guard,stopAfterAction:true));
        }
        var recipe=SinPresetRecipeResolver.Resolve(id);
        if(!recipe.Ok)throw new InvalidDataException(recipe.BlockReason);
        var resolved=SinDryRunPlanner.Plan(recipe.Recipe!);
        var validation=SinPlanValidator.Validate(resolved);
        if(!validation.StructurallyValid)throw new InvalidDataException("Invalid curated recipe");
        var step=resolved.Steps.Single();
        if(step.BodyOps.Count==0)throw new InvalidDataException("No compiled actions");
        var actions=recipe.Recipe!.Nodes.Single().Actions;
        var body=CompileActions(actions);
        if(curse==3){
            // A queued Haste command would be overwritten by the selected Slow
            // command. Apply the self status, then select one offensive action.
            body=Grant(0x38,255).Concat(CompileActions(actions.Skip(1))).ToList();
        }
        if(curse==4){
            // One Nul block, not255 blocks. Support-only fields may continue into
            // the native turn without replacing its selected attack.
            body=Grant(0x30,255).Concat(Grant(0x37,255)).Concat(Grant(0x34,1)).Concat(Grant(0x35,1)).ToList();
            return Once(mon,turn.WorkerIndex,turn.EntrypointIndex,body,id);
        }
        return Require(SinSandboxApplySession.TryEmitRawAction(mon,id,body,entrypointOverride:turn.EntrypointIndex,
            guardOverride:step.GuardOps.Count>0?step.GuardOps:null,stopAfterAction:curse==3||curse>=5));
    }
    private static List<AiInstruction> Grant(ushort field,ushort value)=>new(){Op(0xAE,0xFFF3),Op(0xAE,field),Op(0xAE,value),Op(0xD8,0x7018)};
    private static List<AiInstruction> CompileActions(IEnumerable<SinAction> actions)
    {
        var result=new List<AiInstruction>();
        foreach(var action in actions)switch(action)
        {
            case SinAction.PerformCommand command:
                result.AddRange(new[]{Op(0xAE,command.Target),Op(0xAE,command.CommandOperand),Op(0xD8,command.Force?(ushort)0x705A:(ushort)0x700B)});break;
            case SinAction.PerformCommandOnRandomFrontlineChr command:
                result.AddRange(new[]{Op(0xAE,0xFFF2),Op(0xAE,4),Op(0xAE,0),Op(0xAE,0),Op(0xB5,0x7010),Op(0xAE,command.CommandOperand),Op(0xD8,command.Force?(ushort)0x705A:(ushort)0x700B)});break;
            case SinAction.GrantChrProperty property:
                if(property.Target!=0xFFF3)throw new InvalidDataException("Unproved direct status target");
                result.AddRange(Grant(property.FieldId,property.Value));break;
            default:throw new InvalidDataException("Unspecified curse action "+action.GetType().Name);
        }
        return result;
    }
    private static void VerifyOpeningVeil(byte[] original,byte[] baked)
    {
        var before=AiScript_File.Read(Ai(original));var after=AiScript_File.Read(Ai(baked));
        if(!AiWorkerMapping.TryResolveCombatOnTurn(original,before,out var turn,out string error))throw new InvalidDataException(error);
        var worker=after.Workers[turn.WorkerIndex];var old=before.Workers[turn.WorkerIndex];
        int latch=before.Variables.Count;
        if(after.Variables.Count!=latch+1)throw new InvalidDataException("Opening Veil must reserve exactly one private latch");
        var variables=new Dictionary<int,int>{{latch,123}};
        var instructions=after.Instructions.ToDictionary(x=>x.Offset-after.ScriptStart);
        List<(int Target,int Field,int Value)> Trace(int entry)
        {
            var effects=new List<(int,int,int)>();var stack=new Stack<int>();int pc=worker.Entrypoints[entry];
            for(int budget=0;budget<64;++budget)
            {
                // The wrapper must rejoin the unchanged original entrypoint.
                if(pc==old.Entrypoints[entry])return effects;
                if(!instructions.TryGetValue(pc,out var op))throw new InvalidDataException("Invalid opening wrapper target");
                pc+=op.Length;
                switch(op.Opcode)
                {
                    case 0xAE:stack.Push(op.Operand);break;
                    case 0x9F:if(op.Operand!=latch)throw new InvalidDataException("Opening wrapper read an existing variable");stack.Push(variables[latch]);break;
                    case 0xA0:if(op.Operand!=latch)throw new InvalidDataException("Opening wrapper wrote an existing variable");variables[latch]=stack.Pop();break;
                    case 0x06:{int right=stack.Pop(),left=stack.Pop();stack.Push(left==right?1:0);break;}
                    case 0xD7:if(stack.Pop()==0)pc=worker.JumpTargets[op.Operand];break;
                    case 0xB0:pc=worker.JumpTargets[op.Operand];break;
                    case 0xD8:
                        if(op.Operand!=0x7018)throw new InvalidDataException("Unexpected opening native call");
                        int value=stack.Pop(),field=stack.Pop(),target=stack.Pop();effects.Add((target,field,value));break;
                    default:throw new InvalidDataException($"Unexpected opening opcode {op.Opcode:X2}");
                }
            }
            throw new InvalidDataException("Opening wrapper failed to rejoin the original script");
        }
        if(Trace(0).Count!=0 || variables[latch]!=0)throw new InvalidDataException("Opening latch is not initialized independently");
        var expected=SinPilotRecipes.TurnOneSelfBuff().Nodes.Single().Actions.Cast<SinAction.GrantChrProperty>()
            .Select(x=>((int)x.Target,(int)x.FieldId,(int)x.Value)).ToArray();
        var actual=Trace(turn.EntrypointIndex);
        if(!actual.SequenceEqual(expected) || variables[latch]!=1 || Trace(turn.EntrypointIndex).Count!=0)
            throw new InvalidDataException("Opening Veil must grant all three statuses once, on its first turn");
        for(int entry=1;entry<old.Entrypoints.Count;++entry)if(entry!=turn.EntrypointIndex && worker.Entrypoints[entry]!=old.Entrypoints[entry])
            throw new InvalidDataException("Opening Veil changed an unrelated event entrypoint");
    }
    private static void VerifyTurnCommandOverride(byte[] original,byte[] baked,int curse)
    {
        var before=AiScript_File.Read(Ai(original));var after=AiScript_File.Read(Ai(baked));
        if(!AiWorkerMapping.TryResolveCombatOnTurn(original,before,out var turn,out string error))throw new InvalidDataException(error);
        var recipe=SinPresetRecipeResolver.Resolve($"UNI-{curse:D3}");
        var step=SinDryRunPlanner.Plan(recipe.Recipe!).Steps.Single();
        var worker=after.Workers[turn.WorkerIndex];int start=worker.Entrypoints[turn.EntrypointIndex];
        int guardBytes=step.GuardOps.Count>0?step.GuardOps.Sum(x=>x.Length):3;
        var ops=after.Instructions.ToDictionary(x=>x.Offset-after.ScriptStart);
        var branch=ops[start+guardBytes];var end=ops[start+guardBytes+3+step.BodyOps.Sum(x=>x.Length)];
        if(branch.Opcode!=0xD7 || end.Opcode!=0x3C || worker.JumpTargets[branch.Operand]!=before.Workers[turn.WorkerIndex].Entrypoints[turn.EntrypointIndex])
            throw new InvalidDataException("An active curse command must return before the vanilla attack can overwrite it; a failed guard must rejoin vanilla");
        if(curse==5 && !step.BodyOps.Any(x=>x.Opcode==0xAE && x.Operand==0x610C))
            throw new InvalidDataException("Frost-Flood must select its verified command268");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Export(string source,string output,string editor)
    {
        var roster=new (ushort Monster,byte Mask)[]{(3,0x4D),(26,0x0F),(33,0x27),(81,0x3D),(217,0x6F),(4,0x0F),(12,0x2F),(19,0x3D),(37,0x2F),(87,0xAD)};
        var records=new List<(ushort Monster,byte Curse,ulong BaseHash,byte[] Ai)>();
        var report=new List<object>();
        foreach(var (monster,mask) in roster)
        {
            string file=Path.Combine(source,$"_m{monster:D3}",$"m{monster:D3}.bin");byte[] original=File.ReadAllBytes(file);byte[] baseline=Ai(original);
            for(int curse=1;curse<=8;++curse)
            {
                if((mask&(1<<(curse-1)))==0)continue;
                byte[] baked=Emit(original,curse),ai=Ai(baked);
                SinBehaviorContract.Verify(original,baked,curse);
                if(curse==1)VerifyOpeningVeil(original,baked);
                if(curse>=5)VerifyTurnCommandOverride(original,baked,curse);
                if(ai.Length>65536)throw new InvalidDataException("AI profile is too large");
                var parsed=AiScript_File.Read(ai);
                if(!parsed.CodeWalkClosedExactly || parsed.UnknownOpcodes.Count!=0)throw new InvalidDataException("Bytecode verification failed");
                int beforeWorkers=U32(original,8),beforeStats=U32(original,12),afterWorkers=U32(baked,8),afterStats=U32(baked,12);
                if(!original.AsSpan(beforeWorkers,beforeStats-beforeWorkers).SequenceEqual(baked.AsSpan(afterWorkers,afterStats-afterWorkers)))throw new InvalidDataException("Worker metadata changed outside the AI view");
                if(!original.AsSpan(beforeStats).SequenceEqual(baked.AsSpan(afterStats)))throw new InvalidDataException("Non-AI monster content changed");
                File.WriteAllBytes(Path.Combine(output,$"m{monster:D3}-uni{curse:D3}.ai"),ai);
                records.Add((monster,(byte)curse,Hash(baseline),ai));
                report.Add(new{monster,curse,originalAiLength=baseline.Length,aiLength=ai.Length,originalAiHash=Hash(baseline).ToString("X16"),aiHash=Hash(ai).ToString("X16"),workers=parsed.Workers.Count,privateLengths=parsed.Workers.Select(w=>w.PrivateDataLength)});
            }
        }
        string pack=Path.Combine(output,"_sin-ai-v1.bin");
        using(var stream=File.Create(pack))using(var writer=new BinaryWriter(stream,Encoding.ASCII))
        {
            writer.Write(Encoding.ASCII.GetBytes("SINAI001"));writer.Write((uint)records.Count);
            foreach(var record in records){writer.Write(record.Monster);writer.Write(record.Curse);writer.Write((byte)0);writer.Write(record.BaseHash);writer.Write((uint)record.Ai.Length);writer.Write(Hash(record.Ai));writer.Write(record.Ai);}
        }
        File.WriteAllText(Path.Combine(output,"manifest.json"),JsonSerializer.Serialize(new{author="Jarvis-HOOK",editorAssemblySha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(editor))),packSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(pack))),profiles=report},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"SIN export: {records.Count} profiles; all bytecode and non-AI preservation checks passed.");return 0;
    }
}
