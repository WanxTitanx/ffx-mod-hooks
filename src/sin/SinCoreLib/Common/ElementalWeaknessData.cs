using System;
using Xe.BinaryMapper;

namespace SinCoreLib.Common
{
    public class ElementalWeaknessData
    {
        [Data] public Element_Flags Absorb { get; set; }
        [Data] public Element_Flags Immune { get; set; }
        [Data] public Element_Flags Resist { get; set; }
        [Data] public Element_Flags Weak { get; set; }
    }

    [Flags]
    public enum Element_Flags : byte
    {
        Fire = 0x01,
        Blizzard = 0x02,
        Thunder = 0x04,
        Water = 0x08,
        Holy = 0x10,
        Earth = 0x20,
        Wind = 0x40,
        Dark = 0x80,
    }
}
