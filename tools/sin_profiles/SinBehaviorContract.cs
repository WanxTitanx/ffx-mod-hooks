using FFXProjectEditor.FfxLib.Ai;

internal static class SinBehaviorContract
{
    private readonly record struct Effect(ushort Call,int Target,int Value,int Extra=0);
    public static void Verify(byte[] original,byte[] edited,int curse)
    {
        var before=AiScript_File.Read(AiScript_File.SliceAiFileFromMonster(original)!);
        var after=AiScript_File.Read(AiScript_File.SliceAiFileFromMonster(edited)!);
        AiEventHook hook;
        bool mapped=curse==2?AiWorkerMapping.TryResolveCombatOnHit(original,before,out hook,out _):
            AiWorkerMapping.TryResolveCombatOnTurn(original,before,out hook,out _);
        if(!mapped)throw new InvalidDataException("Missing original event contract");
        var worker=after.Workers[hook.WorkerIndex];
        int originalEntry=before.Workers[hook.WorkerIndex].Entrypoints[hook.EntrypointIndex];
        var ops=after.Instructions.ToDictionary(x=>x.Offset-after.ScriptStart);
        (List<Effect> Effects,bool Terminal) Trace(bool admitted,int latchValue=0)
        {
            var effects=new List<Effect>();var stack=new Stack<int>();
            var vars=new Dictionary<int,int>();for(int i=before.Variables.Count;i<after.Variables.Count;++i)vars[i]=latchValue;
            int pc=worker.Entrypoints[hook.EntrypointIndex];
            for(int budget=0;budget<160;++budget)
            {
                if(pc==originalEntry)return (effects,false);
                if(!ops.TryGetValue(pc,out var op))throw new InvalidDataException("Invalid curse control flow");
                pc+=op.Length;
                switch(op.Opcode)
                {
                    case 0xAE:stack.Push(op.Operand);break;
                    case 0x9F:stack.Push(vars[op.Operand]);break;
                    case 0xA0:vars[op.Operand]=stack.Pop();break;
                    case 0x06:{var r=stack.Pop();var l=stack.Pop();stack.Push(l==r?1:0);break;}
                    case 0xD7:if(stack.Pop()==0)pc=worker.JumpTargets[op.Operand];break;
                    case 0xB0:pc=worker.JumpTargets[op.Operand];break;
                    case 0x3C:return (effects,true);
                    case 0xB5:
                        if(op.Operand==0x70E0)stack.Push(admitted?1:0);
                        else if(op.Operand==0x700F){stack.Pop();stack.Pop();stack.Push(admitted?1:0);}
                        else if(op.Operand==0x7010){var any=stack.Pop();var zero=stack.Pop();var alive=stack.Pop();var target=stack.Pop();if(target!=0xFFF2||alive!=4||zero!=0||any!=0)throw new InvalidDataException("Random hostile target must be a living frontline character");stack.Push(1);}
                        else throw new InvalidDataException($"Unspecified curse return call {op.Operand:X4}");
                        break;
                    case 0xD8:
                        if(op.Operand==0x7018){var value=stack.Pop();var field=stack.Pop();var target=stack.Pop();effects.Add(new Effect(0x7018,target,field,value));}
                        else if(op.Operand==0x700B||op.Operand==0x705A){var command=stack.Pop();var target=stack.Pop();effects.Add(new Effect(op.Operand,target,command));}
                        else throw new InvalidDataException($"Unspecified curse action call {op.Operand:X4}");
                        break;
                    default:throw new InvalidDataException($"Unspecified curse opcode {op.Opcode:X2}");
                }
            }
            throw new InvalidDataException("Curse handler did not terminate within its bounded contract");
        }
        var expected=curse switch
        {
            1=>new[]{new Effect(0x7018,0xFFF3,0x38,255),new Effect(0x7018,0xFFF3,0x31,255),new Effect(0x7018,0xFFF3,0x30,255)},
            2=>new[]{new Effect(0x700B,0xFFEF,0x6110)},
            3=>new[]{new Effect(0x7018,0xFFF3,0x38,255),new Effect(0x705A,1,0x3038)},
            4=>new[]{new Effect(0x7018,0xFFF3,0x30,255),new Effect(0x7018,0xFFF3,0x37,255),new Effect(0x7018,0xFFF3,0x34,1),new Effect(0x7018,0xFFF3,0x35,1)},
            5=>new[]{new Effect(0x705A,0xFFF2,0x610C)},
            6=>new[]{new Effect(0x705A,0xFFF2,0x610D)},
            7=>new[]{new Effect(0x705A,0xFFF3,0x610E)},
            8=>new[]{new Effect(0x705A,0xFFF1,0x610F)},
            _=>throw new InvalidDataException("Unknown curse")
        };
        var fired=Trace(true);
        if(!fired.Effects.SequenceEqual(expected))throw new InvalidDataException($"UNI-{curse:D3} behavior mismatch: {string.Join("; ",fired.Effects)}");
        if((curse==2||curse==3||curse>=5)&&!fired.Terminal)throw new InvalidDataException($"UNI-{curse:D3} lets vanilla replace its selected action");
        if((curse==2||curse==3||curse==7||curse==8)&&Trace(false).Effects.Count!=0)throw new InvalidDataException("Failed guard still applied a curse");
        if((curse==1||curse==4)&&Trace(true,1).Effects.Count!=0)throw new InvalidDataException("Opening curse repeated");
    }
}
