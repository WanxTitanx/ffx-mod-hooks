using System.Buffers.Binary;

namespace FFXProjectEditor.FfxLib.Save;

// Only the two byte accessors used by the linked, unchanged Editor class.
public sealed class FfxSaveCore
{
    public const int DataSize = 25848;
    public byte[] Data { get; }

    public FfxSaveCore(byte[] data)
    {
        if (data.Length != DataSize)
            throw new ArgumentException($"Expected {DataSize} payload bytes.", nameof(data));
        Data = data;
    }

    public int ReadInt32Le(int offset, int byteCount) => byteCount switch
    {
        1 => Data[offset],
        2 => BinaryPrimitives.ReadUInt16LittleEndian(Data.AsSpan(offset, 2)),
        _ => throw new ArgumentOutOfRangeException(nameof(byteCount)),
    };

    public void WriteInt32Le(int offset, int value, int byteCount)
    {
        switch (byteCount)
        {
            case 1:
                Data[offset] = checked((byte)value);
                break;
            case 2:
                BinaryPrimitives.WriteUInt16LittleEndian(Data.AsSpan(offset, 2), checked((ushort)value));
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(byteCount));
        }
    }
}
