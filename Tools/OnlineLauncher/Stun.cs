using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;

namespace ZHReforgedOnline;

/// <summary>
/// The address the internet sees this machine at, asked of a public STUN server (RFC 5389), as the
/// game's StunClient.cpp does; the launcher only needs it to show the game code before the game starts.
/// </summary>
public static class Stun
{
    private static readonly (string Host, int Port)[] Servers =
    {
        ("stun.l.google.com", 19302),
        ("stun.cloudflare.com", 3478),
    };

    private const uint MagicCookie = 0x2112A442;

    public static async Task<uint?> QueryPublicIPAsync(int timeoutMs = 1500)
    {
        foreach (var (host, port) in Servers)
        {
            try
            {
                var addresses = await Dns.GetHostAddressesAsync(host);
                var server = addresses.FirstOrDefault(a => a.AddressFamily == AddressFamily.InterNetwork);
                if (server == null)
                    continue;

                using var udp = new UdpClient(AddressFamily.InterNetwork);
                var transaction = RandomNumberGenerator.GetBytes(12);
                var request = new byte[20];
                request[1] = 0x01; // Binding request
                request[4] = 0x21; request[5] = 0x12; request[6] = 0xA4; request[7] = 0x42;
                Buffer.BlockCopy(transaction, 0, request, 8, 12);
                await udp.SendAsync(request, request.Length, new IPEndPoint(server, port));

                using var cts = new CancellationTokenSource(timeoutMs);
                var reply = await udp.ReceiveAsync(cts.Token);
                var ip = Parse(reply.Buffer, transaction);
                if (ip != null)
                    return ip;
            }
            catch (Exception)
            {
                // the next server
            }
        }
        return null;
    }

    private static uint? Parse(byte[] data, byte[] transaction)
    {
        if (data.Length < 20 || data[0] != 0x01 || data[1] != 0x01)
            return null;
        for (int i = 0; i < 12; ++i)
            if (data[8 + i] != transaction[i])
                return null;

        int length = (data[2] << 8) | data[3];
        int end = Math.Min(data.Length, 20 + length);
        uint? mapped = null;
        for (int pos = 20; pos + 4 <= end;)
        {
            int type = (data[pos] << 8) | data[pos + 1];
            int size = (data[pos + 2] << 8) | data[pos + 3];
            int value = pos + 4;
            if (value + size > end)
                break;
            if (size >= 8 && data[value + 1] == 0x01)
            {
                uint address = (uint)((data[value + 4] << 24) | (data[value + 5] << 16) | (data[value + 6] << 8) | data[value + 7]);
                if (type == 0x0020)
                    return address ^ MagicCookie; // XOR-MAPPED-ADDRESS
                if (type == 0x0001)
                    mapped = address; // MAPPED-ADDRESS
            }
            pos = value + ((size + 3) & ~3);
        }
        return mapped;
    }
}
