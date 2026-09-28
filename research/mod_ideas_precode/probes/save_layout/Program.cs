using System.Buffers.Binary;
using System.Security.Cryptography;
using FFXProjectEditor.FfxLib.Save;

internal static class Program
{
    private const string ExpectedFixtureSha256 =
        "6e2a617b58cc058a72526f20a31bf00b4d79847f7784ce5845935538e77af0b3";

    public static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: save_layout_probe <local genuine 26880-byte PC FFX save fixture>");
            return 2;
        }

        byte[] file = File.ReadAllBytes(args[0]);
        string sha = Convert.ToHexString(SHA256.HashData(file)).ToLowerInvariant();
        if (file.Length != 26880 || sha != ExpectedFixtureSha256)
        {
            Console.Error.WriteLine($"Fixture identity mismatch: size={file.Length} sha256={sha}");
            return 2;
        }

        var slots = FfxSaveEquipment.BuildSlotList();
        if (slots.Count != 200 || FfxSaveEquipment.SlotStride != 22 || slots[0].AbsoluteBase != 17628)
        {
            Console.Error.WriteLine("Unexpected inventory layout in linked Editor source.");
            return 1;
        }

        int occupied = 0;
        int alignedPlausible = 0;
        int editorPlausible = 0;
        foreach (var slot in slots)
        {
            if (file[slot.AbsoluteBase + 2] == 0)
                continue;
            occupied++;
            for (int i = 0; i < 4; i++)
            {
                ushort alignedValue = BinaryPrimitives.ReadUInt16LittleEndian(
                    file.AsSpan(slot.AbsoluteBase + 14 + 2 * i, 2));
                ushort editorValue = BinaryPrimitives.ReadUInt16LittleEndian(
                    file.AsSpan(slot.AbsoluteBase + 15 + 2 * i, 2));
                alignedPlausible += PlausibleAutoAbility(alignedValue) ? 1 : 0;
                editorPlausible += PlausibleAutoAbility(editorValue) ? 1 : 0;
            }
        }
        if (occupied != 146 || alignedPlausible != 575 || editorPlausible != 1)
        {
            Console.Error.WriteLine("Aggregate fixture layout counts changed.");
            return 1;
        }

        int baseOffset = slots[0].AbsoluteBase;
        ushort[] aligned = Enumerable.Range(0, 4)
            .Select(i => BinaryPrimitives.ReadUInt16LittleEndian(file.AsSpan(baseOffset + 14 + 2 * i, 2)))
            .ToArray();
        if (!aligned.SequenceEqual(new ushort[] { 0x8063, 0x8064, 0x802A, 0x8000 }))
        {
            Console.Error.WriteLine("Pinned fixture does not contain the expected aligned abilities.");
            return 1;
        }

        var core = new FfxSaveCore(file.AsSpan(0, FfxSaveCore.DataSize).ToArray());
        var snapshot = FfxSaveEquipmentSnapshot.Read(core, slots[0], 0);
        int[] editorRead = { snapshot.Auto1, snapshot.Auto2, snapshot.Auto3, snapshot.Auto4 };
        if (!editorRead.SequenceEqual(new[] { 0x6480, 0x2A80, 0x0080, 0x2380 }))
        {
            Console.Error.WriteLine("The linked Editor reader no longer reproduces the recorded shift.");
            return 1;
        }

        var synthetic = new FfxSaveCore(new byte[FfxSaveCore.DataSize]);
        synthetic.Data[baseOffset + FfxSaveEquipment.SlotStride] = 0xA5;
        new FfxSaveEquipmentSnapshot { SlotBase = baseOffset, Auto4 = 0x1234 }.Write(synthetic);
        if (synthetic.Data[baseOffset + 21] != 0x34 ||
            synthetic.Data[baseOffset + FfxSaveEquipment.SlotStride] != 0x12)
        {
            Console.Error.WriteLine("The linked Editor writer no longer reproduces the cross-slot write.");
            return 1;
        }

        Console.WriteLine($"REPRODUCED_RT0 linked FfxSaveEquipment.cs fixture_sha256={sha}");
        Console.WriteLine("slot0 aligned +14/+16/+18/+20: " +
                          string.Join(",", aligned.Select(value => $"0x{value:X4}")));
        Console.WriteLine("slot0 Editor +15/+17/+19/+21: " +
                          string.Join(",", editorRead.Select(value => $"0x{value:X4}")));
        Console.WriteLine($"occupied={occupied} plausible_aligned={alignedPlausible}/584 plausible_editor={editorPlausible}/584");
        Console.WriteLine("Auto4=0x1234 wrote 0x12 at the first byte of slot1; 4/4 probe checks passed.");
        return 0;
    }

    private static bool PlausibleAutoAbility(ushort value) =>
        value == 0x00FF || (value >= 0x8000 && value <= 0x807F);
}
