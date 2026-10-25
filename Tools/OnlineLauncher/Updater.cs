using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Text.Json;

namespace ZHReforgedOnline;

/// <summary>
/// The online build as published on the fork's GitHub releases: the launcher alone is enough, it
/// fetches the game itself. Each release is tagged online-&lt;commit&gt; and carries the game as
/// ZHReforgedOnline-game.zip, whose version.txt ends in the same commit.
/// </summary>
public static class Updater
{
    private const string Repository = "Robocnop/CnCGeneralsZH-Reforged";
    private const string GameAsset = "ZHReforgedOnline-game.zip";
    private const string TagPrefix = "online-";

    public sealed record Release(string Tag, string Build, string DownloadUrl, long Size);

    /// <summary>Where a downloaded build is unpacked.</summary>
    public static string CacheFolder => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ZHReforgedOnline", "payload");

    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("ZHReforgedOnline-Launcher");
        return client;
    }

    /// <summary>The commit a version reads as: "v3.3.0+903e058" and "online-903e058" are both 903e058.</summary>
    public static string? BuildOf(string? version)
    {
        if (string.IsNullOrEmpty(version))
            return null;
        var plus = version.LastIndexOf('+');
        if (plus >= 0)
            return version[(plus + 1)..];
        return version.StartsWith(TagPrefix, StringComparison.OrdinalIgnoreCase) ? version[TagPrefix.Length..] : version;
    }

    /// <summary>The latest release, or null when GitHub cannot be reached or has none.</summary>
    public static async Task<Release?> LatestAsync()
    {
        try
        {
            using var response = await Http.GetAsync($"https://api.github.com/repos/{Repository}/releases/latest");
            if (!response.IsSuccessStatusCode)
                return null;
            using var doc = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var tag = doc.RootElement.GetProperty("tag_name").GetString();
            if (tag == null)
                return null;
            foreach (var asset in doc.RootElement.GetProperty("assets").EnumerateArray())
            {
                if (asset.GetProperty("name").GetString() == GameAsset)
                {
                    return new Release(tag, BuildOf(tag)!, asset.GetProperty("browser_download_url").GetString()!,
                        asset.GetProperty("size").GetInt64());
                }
            }
        }
        catch (Exception)
        {
            // offline: whatever is here already will do
        }
        return null;
    }

    /// <summary>Downloads the release's game and unpacks it into CacheFolder, replacing what was there.</summary>
    public static async Task DownloadAsync(Release release, IProgress<string> progress)
    {
        var root = Path.GetDirectoryName(CacheFolder)!;
        Directory.CreateDirectory(root);
        var zip = Path.Combine(root, GameAsset);

        using (var response = await Http.GetAsync(release.DownloadUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            var total = response.Content.Headers.ContentLength ?? release.Size;
            await using var input = await response.Content.ReadAsStreamAsync();
            await using var output = File.Create(zip);
            var buffer = new byte[1 << 16];
            long done = 0;
            int lastPercent = -1;
            int read;
            while ((read = await input.ReadAsync(buffer)) > 0)
            {
                await output.WriteAsync(buffer.AsMemory(0, read));
                done += read;
                int percent = total > 0 ? (int)(done * 100 / total) : 0;
                if (percent != lastPercent)
                {
                    lastPercent = percent;
                    progress.Report($"Téléchargement : {percent} % ({done / (1024 * 1024)} Mo)");
                }
            }
        }

        progress.Report("Décompression…");
        await Task.Run(() =>
        {
            if (Directory.Exists(CacheFolder))
                Directory.Delete(CacheFolder, true);
            ZipFile.ExtractToDirectory(zip, CacheFolder);
            File.Delete(zip);
        });
    }
}
