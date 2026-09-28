using System.Security.Cryptography;
using FFXProjectEditor.FfxLib.Ability;
using FFXProjectEditor.FfxLib.Dictionaries;

internal static class AeonElementProbe
{
    private const string ExpectedCommandSha256 =
        "db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429";
    private sealed record RowSpec(int Id, string Name, Character_Enum Owner, byte Element);

    private static readonly RowSpec[] Rows =
    {
        new(203, "Valefor Attack", Character_Enum.Valefor, 0x08),
        new(204, "Sonic Wings", Character_Enum.Valefor, 0x08),
        new(205, "Energy Blast", Character_Enum.Valefor, 0x08),
        new(206, "Energy Ray", Character_Enum.Valefor, 0x08),
        new(216, "Bahamut Attack", Character_Enum.Bahamut, 0x10),
        new(217, "Impulse", Character_Enum.Bahamut, 0x10),
        new(218, "Mega Flare", Character_Enum.Bahamut, 0x10),
    };

    public static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: aeon_element_probe <local FFX command.bin>");
            return 2;
        }

        byte[] original = File.ReadAllBytes(args[0]);
        string sha = Convert.ToHexString(SHA256.HashData(original)).ToLowerInvariant();
        if (sha != ExpectedCommandSha256)
        {
            Console.Error.WriteLine($"Command fixture identity mismatch: sha256={sha}");
            return 2;
        }
        var commands = Ability_Command.ReadList(original, hasExtraInfo: true);
        if (commands.Count <= Rows.Max(row => row.Id))
        {
            Console.Error.WriteLine($"Command table is too short: {commands.Count} rows.");
            return 1;
        }

        var expectedDiffs = new List<int>();
        foreach (RowSpec spec in Rows)
        {
            Ability_Command row = commands[spec.Id];
            byte before = (byte)row.ElementFlgs;
            byte after = (byte)(before | spec.Element);
            Console.WriteLine($"row={spec.Id} name={spec.Name} owner={row.CharacterUser} element=0x{before:X2}->0x{after:X2} formula={row.DamageFormula} power={row.AttackPower}");
            if (row.CharacterUser != spec.Owner)
            {
                Console.Error.WriteLine($"Owner mismatch in command #{spec.Id}.");
                return 1;
            }
            row.ElementFlgs = (Ability_Command.ElementFlags)after;
            if (after != before)
                expectedDiffs.Add(0x14 + spec.Id * 0x60 + 0x2D);
        }

        byte[] staged = Ability_Command.WriteList(commands, hasExtraInfo: true);
        if (staged.Length != original.Length)
        {
            Console.Error.WriteLine("The Editor writer changed file length.");
            return 1;
        }
        int[] actualDiffs = Enumerable.Range(0, original.Length)
            .Where(i => original[i] != staged[i]).ToArray();
        if (!expectedDiffs.SequenceEqual(actualDiffs))
        {
            Console.Error.WriteLine("Element edit affected unexpected bytes.");
            Console.Error.WriteLine("Expected: " + string.Join(",", expectedDiffs.Select(x => $"0x{x:X}")));
            Console.Error.WriteLine("Actual: " + string.Join(",", actualDiffs.Select(x => $"0x{x:X}")));
            return 1;
        }

        var reread = Ability_Command.ReadList(staged, hasExtraInfo: true);
        if (Rows.Any(spec => (((byte)reread[spec.Id].ElementFlgs & spec.Element) != spec.Element)))
        {
            Console.Error.WriteLine("Edited element flags failed reread.");
            return 1;
        }

        Console.WriteLine($"RT0_PASS rows={Rows.Length} byte_diffs={actualDiffs.Length} source_sha256={sha} staged_sha256={Convert.ToHexString(SHA256.HashData(staged)).ToLowerInvariant()}");
        Console.WriteLine("The modified command.bin exists only in memory; game behavior and animations remain untested.");

        var odCommands = Ability_Command.ReadList(original, hasExtraInfo: true);
        Ability_Command ronsoRage = odCommands[104];
        byte oldCost = ronsoRage.CostOverdrive;
        byte newCost = oldCost == 40 ? (byte)41 : (byte)40;
        if (ronsoRage.CharacterUser != Character_Enum.Kimahri)
        {
            Console.Error.WriteLine("Ronso Rage #104 has an unexpected owner.");
            return 1;
        }
        ronsoRage.CostOverdrive = newCost;
        byte[] odStaged = Ability_Command.WriteList(odCommands, hasExtraInfo: true);
        int[] odDiffs = Enumerable.Range(0, original.Length)
            .Where(i => original[i] != odStaged[i]).ToArray();
        int expectedCostOffset = 0x14 + 104 * 0x60 + 0x26;
        if (!odDiffs.SequenceEqual(new[] { expectedCostOffset }) ||
            Ability_Command.ReadList(odStaged, hasExtraInfo: true)[104].CostOverdrive != newCost)
        {
            Console.Error.WriteLine("Partial OD cost edit was not byte-local or failed reread.");
            return 1;
        }
        Console.WriteLine($"OD_COST_RT0_PASS cmd=104 owner=Kimahri cost={oldCost}->{newCost} file_offset=0x{expectedCostOffset:X}; menu-ready gate remains separate.");

        var ownerCommands = Ability_Command.ReadList(original, hasExtraInfo: true);
        ownerCommands[203].CharacterUser = Character_Enum.Bahamut;
        byte[] ownerStaged = Ability_Command.WriteList(ownerCommands, hasExtraInfo: true);
        int[] ownerDiffs = Enumerable.Range(0, original.Length)
            .Where(i => original[i] != ownerStaged[i]).ToArray();
        int expectedOwnerOffset = 0x14 + 203 * 0x60 + 0x19;
        if (!ownerDiffs.SequenceEqual(new[] { expectedOwnerOffset }) ||
            Ability_Command.ReadList(ownerStaged, hasExtraInfo: true)[203].CharacterUser != Character_Enum.Bahamut)
        {
            Console.Error.WriteLine("CharacterUser edit was not byte-local or failed reread.");
            return 1;
        }
        Console.WriteLine($"COMMAND_OWNER_RT0_PASS cmd=203 owner=Valefor->Bahamut file_offset=0x{expectedOwnerOffset:X}; menu behavior remains untested.");
        return 0;
    }
}
