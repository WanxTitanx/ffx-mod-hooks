using System.Globalization;
using System.IO.MemoryMappedFiles;

const string MmfName = "Local\\FFXHooksBlock_v1";
const uint Magic = 0x48584646; // 'FFXH'
const long OMagic = 0;
const long OVersion = 4;
const long OMusicOverride = 8;
const long OMusicSeq = 12;
const long OElementFlagsExt = 16;

if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
{
    PrintUsage();
    return args.Length == 0 ? 1 : 0;
}

using var accessor = OpenBlock();
if (accessor is null)
{
    return 2;
}

var mode = args[0].ToLowerInvariant();
switch (mode)
{
    case "status":
        PrintStatus(accessor);
        return 0;

    case "music":
        if (args.Length < 2)
        {
            Console.Error.WriteLine("music requires a track index 0..0xB5, or clear.");
            return 1;
        }

        if (args[1].Equals("clear", StringComparison.OrdinalIgnoreCase))
        {
            SetMusicOverride(accessor, -1, incrementSeq: true);
            PrintStatus(accessor);
            return 0;
        }

        var track = ParseInt(args[1]);
        if (track is < 0 or > 0xB5)
        {
            Console.Error.WriteLine($"track out of range: {track}. Expected 0..0xB5.");
            return 1;
        }

        SetMusicOverride(accessor, track, incrementSeq: true);
        PrintStatus(accessor);
        return 0;

    case "clear":
        SetMusicOverride(accessor, -1, incrementSeq: true);
        PrintStatus(accessor);
        return 0;

    default:
        Console.Error.WriteLine($"unknown command: {args[0]}");
        PrintUsage();
        return 1;
}

static MemoryMappedViewAccessor? OpenBlock()
{
    try
    {
        var mmf = MemoryMappedFile.OpenExisting(MmfName);
        var accessor = mmf.CreateViewAccessor(0, 256, MemoryMappedFileAccess.ReadWrite);
        var magic = accessor.ReadUInt32(OMagic);
        if (magic != Magic)
        {
            Console.Error.WriteLine($"MMF '{MmfName}' has bad magic 0x{magic:X8}; ffx-hooks.dll not initialized?");
            accessor.Dispose();
            mmf.Dispose();
            return null;
        }

        return accessor;
    }
    catch (FileNotFoundException)
    {
        Console.Error.WriteLine($"MMF '{MmfName}' not found. Start FFX with ffx-hooks.dll loaded first.");
        return null;
    }
}

static void PrintStatus(MemoryMappedViewAccessor accessor)
{
    var magic = accessor.ReadUInt32(OMagic);
    var version = accessor.ReadUInt32(OVersion);
    var musicOverride = accessor.ReadInt32(OMusicOverride);
    var musicSeq = accessor.ReadUInt32(OMusicSeq);
    var elementFlagsExt = accessor.ReadByte(OElementFlagsExt);

    Console.WriteLine($"magic=0x{magic:X8} version={version} musicOverrideTrackIndex={musicOverride} musicSeq={musicSeq} elementFlagsExt=0x{elementFlagsExt:X2}");
}

static void SetMusicOverride(MemoryMappedViewAccessor accessor, int track, bool incrementSeq)
{
    accessor.Write(OMusicOverride, track);
    if (incrementSeq)
    {
        var seq = accessor.ReadUInt32(OMusicSeq);
        accessor.Write(OMusicSeq, unchecked(seq + 1));
    }
}

static int ParseInt(string value)
{
    if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
    {
        return int.Parse(value[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
    }

    return int.Parse(value, CultureInfo.InvariantCulture);
}

static void PrintUsage()
{
    Console.WriteLine("usage:");
    Console.WriteLine("  ffxhooksctl status");
    Console.WriteLine("  ffxhooksctl music <track 0..0xB5>");
    Console.WriteLine("  ffxhooksctl music clear");
    Console.WriteLine("  ffxhooksctl clear");
}
