/* Jarvis-HOOK: isolated i386 experiment; no game process or hooks are used.
 * The runner links a hash-pinned, locally extracted, position-independent routine.
 * The expected model is derived from the disassembly, not external mod code. */
typedef unsigned char u8;
extern int native_element(const void *, const void *, unsigned, int);
static unsigned checks, failed, first_mask, first_flags[4];
static int first_actual,first_expected,first_damage;
static u8 actor[0x800], before[0x800];

static void write_text(const char *s, unsigned n) {
    int result=4;
    __asm__ volatile("int $0x80" : "+a"(result) : "b"(1), "c"(s), "d"(n) : "memory", "cc");
}
static void number(unsigned value) {
    char buf[12]; unsigned n=0;
    do { buf[11-n++]=(char)('0'+value%10); value/=10; } while(value);
    write_text(buf+12-n,n);
}
static void signed_number(int value) { if(value<0){write_text("-",1);number((unsigned)-value);}else number((unsigned)value); }
static int expected(unsigned mask, unsigned weak, unsigned half,
                    unsigned nulls, unsigned absorb, int damage) {
    mask &= 255;
    unsigned w=mask&weak;
    if(w) { for(unsigned bit=1;bit<256;bit<<=1) if(w&bit) damage=damage*3/2; return damage; }
    if(mask & ~(half|nulls|absorb)) return damage;
    if(mask & half & ~(nulls|absorb)) return damage/2;
    if(mask & nulls & ~absorb) return 0;
    if(mask & absorb) return -damage;
    return damage;
}
static void run(unsigned mask,unsigned weak,unsigned half,unsigned nulls,unsigned absorb,int damage) {
    for(unsigned i=0;i<sizeof(actor);++i) actor[i]=before[i]=0xA5;
    actor[0x5DA]=before[0x5DA]=(u8)absorb;
    actor[0x5DB]=before[0x5DB]=(u8)nulls;
    actor[0x5DC]=before[0x5DC]=(u8)half;
    actor[0x5DD]=before[0x5DD]=(u8)weak;
    int actual=native_element(actor,0,mask,damage);
    int want=expected(mask,weak,half,nulls,absorb,damage);
    ++checks;
    if(actual!=want) { if(!failed){first_mask=mask;first_actual=actual;first_expected=want;first_damage=damage;first_flags[0]=weak;first_flags[1]=half;first_flags[2]=nulls;first_flags[3]=absorb;} ++failed; }
    for(unsigned i=0;i<sizeof(actor);++i) if(actor[i]!=before[i]) { ++failed; break; }
}
void _start(void) {
    const int damage[]={0,1,3,1600,-1,-3,-1600};
    /* Every overlapping affinity flag combination on every individual bit. */
    for(unsigned b=1;b<256;b<<=1) for(unsigned c=0;c<16;++c)
        for(unsigned d=0;d<7;++d) run(b,c&1?b:0,c&2?b:0,c&4?b:0,c&8?b:0,damage[d]);
    /* Every canonical affinity assignment across all eight bits: 5^8 cases. */
    for(unsigned state=0;state<390625;++state) {
        unsigned value=state,w=0,h=0,n=0,a=0;
        for(unsigned b=1;b<256;b<<=1) {
            unsigned c=value%5;value/=5;
            if(c==1)w|=b;if(c==2)h|=b;if(c==3)n|=b;if(c==4)a|=b;
        }
        run(255,w,h,n,a,1601);
    }
    /* All input byte masks; overlapping masks; high argument bits do not add elements. */
    for(unsigned m=0;m<256;++m) for(unsigned c=0;c<16;++c) {
        run(m,c&1?0xAA:0,c&2?0xCC:0,c&4?0xF0:0,c&8?0x55:0,-1601);
        run(m|0x100,c&1?0xAA:0,c&2?0xCC:0,c&4?0xF0:0,c&8?0x55:0,1601);
        run(m|0x200,c&1?0xAA:0,c&2?0xCC:0,c&4?0xF0:0,c&8?0x55:0,1601);
    }
    /* Stable, directly reviewable controls: 1000 -> 1500/500/0/-1000 per native bit. */
    write_text("{\"single_bit_results\":[",23);
    for(unsigned b=1;b<256;b<<=1) {
        if(b>1)write_text(",",1);
        write_text("{\"bit\":",7);number(b);write_text(",\"weak\":",8);
        for(unsigned i=0;i<sizeof(actor);++i)actor[i]=0;
        actor[0x5DD]=(u8)b;number((unsigned)native_element(actor,0,b,1000));
        actor[0x5DD]=0;actor[0x5DC]=(u8)b;write_text(",\"half\":",8);number((unsigned)native_element(actor,0,b,1000));
        actor[0x5DC]=0;actor[0x5DB]=(u8)b;write_text(",\"null\":",8);number((unsigned)native_element(actor,0,b,1000));
        actor[0x5DB]=0;actor[0x5DA]=(u8)b;write_text(",\"absorb\":-",11);number((unsigned)-native_element(actor,0,b,1000));write_text("}",1);
    }
    write_text("],\"comparisons\":",16);number(checks);
    write_text(",\"failures\":",12);number(failed);
    write_text(",\"first_mismatch\":[",19);number(first_mask);
    for(unsigned i=0;i<4;++i){write_text(",",1);number(first_flags[i]);}
    write_text(",",1);signed_number(first_damage);write_text(",",1);signed_number(first_actual);
    write_text(",",1);signed_number(first_expected);write_text("]}\n",3);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed?1:0) : "memory");
    __builtin_unreachable();
}
