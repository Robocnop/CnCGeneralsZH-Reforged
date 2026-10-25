using System.IO;
using System.Text.Json;
using Microsoft.Win32;

namespace ZHReforgedOnline;

/// <summary>What the launcher remembers, in %APPDATA%\ZHReforgedOnline\settings.json.</summary>
public sealed class Settings
{
    public string? SourceFolder { get; set; }
    public string? InstallFolder { get; set; }
    public string Language { get; set; } = "french";
    public string? FriendCode { get; set; }

    private static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZHReforgedOnline", "settings.json");

    public static Settings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings();
        }
        catch (Exception)
        {
            // a damaged file starts over
        }
        return new Settings();
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }

    public const string ZeroHourFolderName = "Command & Conquer Generals - Zero Hour";

    /// <summary>A folder that holds Zero Hour: its generals.exe and INIZH.big.</summary>
    public static bool IsZeroHourFolder(string? folder) =>
        !string.IsNullOrEmpty(folder) && File.Exists(Path.Combine(folder, "INIZH.big"));

    /// <summary>
    /// Where Zero Hour is: the official launcher's choice first, then every Steam library.
    /// </summary>
    public static string? FindZeroHour()
    {
        try
        {
            var official = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "zhreforged-launcher", "settings.json");
            if (File.Exists(official))
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(official));
                if (doc.RootElement.TryGetProperty("gameFolder", out var folder))
                {
                    var path = folder.GetString()?.TrimEnd('\\', '/');
                    if (IsZeroHourFolder(path))
                        return path;
                }
            }
        }
        catch (Exception)
        {
        }

        foreach (var library in SteamLibraries())
        {
            var path = Path.Combine(library, "steamapps", "common", ZeroHourFolderName);
            if (IsZeroHourFolder(path))
                return path;
        }
        return null;
    }

    private static IEnumerable<string> SteamLibraries()
    {
        var steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
        if (string.IsNullOrEmpty(steam))
            yield break;
        steam = steam.Replace('/', '\\');
        yield return steam;

        var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (!File.Exists(vdf))
            yield break;
        foreach (var line in File.ReadAllLines(vdf))
        {
            var trimmed = line.Trim();
            if (!trimmed.StartsWith("\"path\"", StringComparison.OrdinalIgnoreCase))
                continue;
            var parts = trimmed.Split('"', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length >= 3)
                yield return parts[^1].Replace(@"\\", @"\");
        }
    }
}
