// Jarvis-HOOK. GPL-3.0 fixture/VM adapted from the same Editor repository;
// see PROVENANCE.md. This bounded semantic test is RT0, not a game emulator.
using FFXProjectEditor.FfxLib.Ai;
using FFXProjectEditor.FfxLib.Ai.Sin;
namespace FfxHooks.SinProfiles;
internal static class SinPostActionContract
{
    static int checks;
    static void Check(bool ok,string label){++checks;if(!ok)throw new InvalidDataException(label);}
    sealed class Trace
    {
        public List<(ushort Target,ushort Command)> Commands=new();
        public List<(ushort Target,ushort Field)> Statuses=new();
        public Dictionary<int,int> Vars=new();public Stack<int> Stack=new();public int Finds;
    }
    static byte[] Fixture(bool shared=false)
    {
        byte[] code={0xAE,0xFA,0xFF,0xAE,0,0x40,0xD8,0x0B,0x70,0xAE,7,0,0xA0,0,0,0x3C,
                     0xAE,0xFA,0xFF,0xAE,2,0x40,0xD8,0x0B,0x70,0x3C};
        const int table=0x80+26;var ai=new byte[192];
        void U32(int off,int value)=>BitConverter.GetBytes(value).CopyTo(ai,off);
        void U16(int off,ushort value)=>BitConverter.GetBytes(value).CopyTo(ai,off);
        U32(0,code.Length);U32(0x10,ai.Length);U16(0x14,1);U32(0x30,0x80);U16(0x36,1);U32(0x38,0x40);
        U16(0x48,2);U16(0x50,4);U32(0x54,0x70);U32(0x58,0x78);U32(0x5C,0x78);U32(0x60,table);U32(0x64,table+8);
        U32(0x70,0x56000000);U32(0x74,1);U32(table,0);U32(table+4,shared?0:16);code.CopyTo(ai,0x80);
        int worker=0x30+ai.Length,stat=worker+32;var mon=new byte[stat+16];
        BitConverter.GetBytes(mon.Length).CopyTo(mon,0);BitConverter.GetBytes(0x30).CopyTo(mon,4);
        BitConverter.GetBytes(worker).CopyTo(mon,8);BitConverter.GetBytes(stat).CopyTo(mon,12);ai.CopyTo(mon,0x30);
        mon[worker]=1;mon[worker+1]=2;mon[worker+5]=2;mon[worker+6]=8;mon[worker+8]=4;
        for(int i=12;i<16;++i)mon[worker+i]=255;mon[worker+16]=1;return mon;
    }
    static Trace Execute(byte[] ai,int entry=0,bool dieAfterNative=false,bool dieAfterFirstExtra=false,
        bool killPrevious=false,int[]? afterHp=null,int missingTarget=0)
    {
        var script=AiScript_File.Read(ai);var trace=new Trace();var worker=script.Workers[0];
        var ops=script.Instructions.ToDictionary(x=>x.Offset-script.ScriptStart);int pc=worker.Entrypoints[entry];
        var hp=new Dictionary<ushort,int>{{0xFFF3,100},{0xFFFA,100},{0xFFF9,100},{0xFFF8,100},{10,100},{11,100},{12,100}};
        for(int budget=0;budget<500;++budget){var op=ops[pc];pc+=op.Length;
            switch(op.Opcode){
                case 0:break;case 0xAE:trace.Stack.Push(op.Operand);break;
                case 0x9F:trace.Stack.Push(trace.Vars.GetValueOrDefault(op.Operand));break;
                case 0xA0:trace.Vars[op.Operand]=trace.Stack.Pop();break;
                case 0x0B:{int r=trace.Stack.Pop(),l=trace.Stack.Pop();trace.Stack.Push(l<r?1:0);break;}
                case 0xB0:pc=worker.JumpTargets[op.Operand];break;
                case 0xD7:if(trace.Stack.Pop()==0)pc=worker.JumpTargets[op.Operand];break;
                case 0xD6:if(trace.Stack.Pop()!=0)pc=worker.JumpTargets[op.Operand];break;
                case 0x3C:return trace;
                case 0xB5 when op.Operand==0x700F:
                    Check(trace.Stack.Pop()==0,"HP getter has the exact numeric field operand");trace.Stack.Push(hp[(ushort)trace.Stack.Pop()]);break;
                case 0xB5 when op.Operand==0x7010:
                    Check(trace.Stack.Pop()==0&&trace.Stack.Pop()==0&&trace.Stack.Pop()==4,"random selection requires a living matching actor");
                    trace.Stack.Pop();trace.Stack.Push(missingTarget!=0?missingTarget:10+trace.Finds);++trace.Finds;break;
                case 0xD8 when op.Operand is 0x700B or 0x705A:{
                    ushort command=(ushort)trace.Stack.Pop(),target=(ushort)trace.Stack.Pop();trace.Commands.Add((target,command));
                    if(command==0x4000){if(dieAfterNative)hp[0xFFF3]=0;if(afterHp is not null){hp[0xFFFA]=afterHp[0];hp[0xFFF9]=afterHp[1];hp[0xFFF8]=afterHp[2];}}
                    else if(trace.Commands.Count==2){if(dieAfterFirstExtra)hp[0xFFF3]=0;if(killPrevious&&hp.ContainsKey(target))hp[target]=0;}
                    break;}
                case 0xD8 when op.Operand==0x7018:
                    Check(trace.Stack.Pop()==1,"status writer applies its exact requested value");ushort field=(ushort)trace.Stack.Pop(),actor=(ushort)trace.Stack.Pop();trace.Statuses.Add((actor,field));break;
                default:throw new InvalidDataException($"Unspecified semantic-test opcode {op.Opcode:X2}/{op.Operand:X4}");
            }
        }throw new InvalidDataException("Post-action sequence exceeded its bounded continuation budget");
    }
    public static int Run()
    {
        foreach(var recipe in SinPostActionPresetCatalog.All){
            string id=recipe.RequiredMonster??(recipe.DamageOnly?"m198":"m001");byte[] mon=Fixture(),copy=(byte[])mon.Clone();
            Check(SinPostActionCompiler.TryBuild(mon,id,recipe.Id,out var edit,out string error)&&edit is not null,error);
            var live=Execute(edit!.AiFile);
            var expected=recipe.ReplayNative?new ushort[]{0x4000,0x4000}:new ushort[]{0x4000}.Concat(recipe.Skills.Select(s=>s.Operand)).ToArray();
            Check(live.Commands.Select(c=>c.Command).SequenceEqual(expected),"the native action precedes the complete authored sequence exactly once");
            Check(live.Vars[0]==7&&live.Stack.Count==0&&mon.SequenceEqual(copy),"native tail/private state and source bytes remain intact");
            Check(Execute(edit.AiFile,1).Commands.Select(c=>c.Command).SequenceEqual(new ushort[]{0x4002}),"non-turn events remain native");
            if(!recipe.DamageOnly){var dead=Execute(edit.AiFile,dieAfterNative:true);
                Check(dead.Commands.Count==1&&dead.Statuses.Count==0&&dead.Vars[0]==7,"a terminal native action skips extra requests and still completes the original tail");}
            if(recipe.Skills.Count>1){var dead=Execute(edit.AiFile,dieAfterFirstExtra:true);
                Check(dead.Commands.Count==2&&dead.Statuses.Count==0,"death during the first extra action cannot enqueue the second action or a later status write");}
            if(recipe.Skills.Any(s=>s.Target==SinUniTarget.Previous)){
                Check(live.Finds==1&&live.Commands[1].Target==live.Commands[2].Target,"same-target pairs never reroll their second target");
                Check(Execute(edit.AiFile,killPrevious:true).Commands.Count==2,"a dead previous target skips the second hit");}
            if(recipe.Skills.All(s=>s.Target is SinUniTarget.RandomFrontline or SinUniTarget.RandomAlly or SinUniTarget.Previous)&&recipe.Skills.Count>0)
                foreach(int none in new[]{-1,65535})Check(Execute(edit.AiFile,missingTarget:none).Commands.Count==1,
                    "a missing living target cannot reach a native actor getter or command request");
            byte[] baked=AiScript_File.SpliceAiFileIntoMonsterGrow(mon,edit.AiFile);
            Check(!SinPostActionCompiler.TryBuild(baked,id,recipe.Id,out _,out _),"rebaking cannot amplify a replay, Zombie write or existing sequence");
        }
        foreach(var health in new[]{new[]{100,100,100},new[]{80,100,90},new[]{110,100,100},new[]{0,0,100}}){
            Check(SinPostActionCompiler.TryBuild(Fixture(),"m200","UNI-040",out var edit,out _),"Fallen Monk flamethrower has the exact exclusive recipe");
            var trace=Execute(edit!.AiFile,afterHp:health);
            var expected=new ushort[]{0xFFFA,0xFFF9,0xFFF8}.Where((_,i)=>health[i]<100);
            Check(trace.Statuses.Select(s=>s.Target).SequenceEqual(expected)&&trace.Statuses.All(s=>s.Field==7),"Fallen Touch applies Zombie only to actors who actually lost HP, including area hits");
        }
        foreach(string preset in new[]{"UNI-021","UNI-022","UNI-023","UNI-040"})
            Check(!SinPostActionCompiler.TryBuild(Fixture(),"m001",preset,out _,out _),"exclusive recipes reject a different monster model");
        Check(!SinPostActionCompiler.TryBuild(Fixture(true),"m001","UNI-015",out _,out _),"shared turn/reaction call sites require isolation rather than gaining another event writer");
        Console.WriteLine($"SIN_POST_ACTION_CONTRACT_RT0 {checks}/{checks} passed");return 0;
    }
}
