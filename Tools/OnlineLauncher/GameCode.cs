namespace ZHReforgedOnline;

/// <summary>
/// The game code the Direct Connect screen shows, the same as GameNetwork/GameCode.cpp: eight
/// characters of Crockford's base32 holding the IPv4 address and a CRC-8 of it.
/// </summary>
public static class GameCode
{
    private const string Crockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    private static byte Crc8(uint ip)
    {
        byte crc = 0;
        for (int i = 3; i >= 0; --i)
        {
            crc ^= (byte)(ip >> (i * 8));
            for (int bit = 0; bit < 8; ++bit)
                crc = (byte)((crc & 0x80) != 0 ? (crc << 1) ^ 0x07 : crc << 1);
        }
        return crc;
    }

    public static string FromIP(uint ip)
    {
        ulong value = ((ulong)ip << 8) | Crc8(ip);
        var chars = new char[9];
        int pos = 0;
        for (int i = 7; i >= 0; --i)
        {
            chars[pos++] = Crockford[(int)((value >> (i * 5)) & 31)];
            if (i == 4)
                chars[pos++] = '-';
        }
        return new string(chars);
    }

    public static bool TryToIP(string? text, out uint ip)
    {
        ip = 0;
        if (text == null)
            return false;
        ulong value = 0;
        int digits = 0;
        foreach (char raw in text)
        {
            if (raw == '-' || char.IsWhiteSpace(raw))
                continue;
            char c = char.ToUpperInvariant(raw);
            if (c == 'O') c = '0';
            if (c == 'I' || c == 'L') c = '1';
            int v = Crockford.IndexOf(c);
            if (v < 0 || digits == 8)
                return false;
            value = (value << 5) | (uint)v;
            ++digits;
        }
        if (digits != 8)
            return false;
        uint address = (uint)(value >> 8);
        if ((byte)(value & 0xff) != Crc8(address))
            return false;
        ip = address;
        return true;
    }

    /// <summary>A code or a dotted IPv4 address, as the game's Join box takes it.</summary>
    public static bool IsJoinTarget(string text)
    {
        if (TryToIP(text, out _))
            return true;
        return System.Net.IPAddress.TryParse(text.Trim(), out var address)
            && address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork
            && text.Trim().Count(c => c == '.') == 3;
    }

    public static string ToDotted(uint ip) =>
        $"{ip >> 24}.{(ip >> 16) & 0xff}.{(ip >> 8) & 0xff}.{ip & 0xff}";
}
