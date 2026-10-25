using System.IO;
using System.Runtime.InteropServices;

namespace ZHReforgedOnline;

/// <summary>
/// Puts this build beside the player's own Zero Hour, never inside it.
///
/// The install folder is a copy of the Zero Hour folder with this build laid over it. The archives
/// and movies, which make up nearly all of the 4 GB and which nothing ever writes to, are hard links
/// to the originals when the two folders are on one drive, so they cost no space; everything else
/// is copied, because the game writes some of its own files (the shader cache) and a write through a
/// link would land in the original. A file of this build replaces its counterpart by deleting the
/// link first, so the original is never touched. Steam's copy, and the official launcher's, stay
/// exactly as they were.
/// </summary>
public static class Installer
{
    /// <summary>The build this launcher carries, next to the exe.</summary>
    public static string PayloadFolder => Path.Combine(AppContext.BaseDirectory, "game");

    private const string VersionFile = "version.txt";
    private const string InstalledMarker = "ZHReforgedOnline.version";

    private static readonly HashSet<string> SkippedFolders = new(StringComparer.OrdinalIgnoreCase)
    {
        ".claude", "Manuals", "RedistInstallers", "ZHReforged-Uninstall", "GeneralsOnlineGameData",
    };

    private static readonly HashSet<string> SkippedFiles = new(StringComparer.OrdinalIgnoreCase)
    {
        "DebugLogFile.txt", "DebugLogFilePrev.txt", "dx11shaders.cache", "MismatchDump.txt",
        "GeneralsOnlineZH.exe", "GeneralsOnlineZH_60.exe", "GenTool_uninstall.exe", "GenToolUpdater.exe",
    };

    private static readonly HashSet<string> LinkedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".big", ".bik",
    };

    // the base game, when it is not in ZH_Generals: the folders Win32BIGFileSystem.cpp looks beside Zero Hour
    private static readonly string[] BaseGameSiblings =
    {
        "Command & Conquer Generals", "Command & Conquer(tm) Generals", "Command and Conquer Generals",
    };

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateHardLink(string lpFileName, string lpExistingFileName, IntPtr lpSecurityAttributes);

    public static string? PayloadVersion() => ReadFirstLine(Path.Combine(PayloadFolder, VersionFile));

    public static string? InstalledVersion(string? installFolder) =>
        string.IsNullOrEmpty(installFolder) ? null : ReadFirstLine(Path.Combine(installFolder, InstalledMarker));

    public static bool IsInstalled(string? installFolder) =>
        !string.IsNullOrEmpty(installFolder) && File.Exists(Path.Combine(installFolder, "generals.exe"))
        && InstalledVersion(installFolder) != null;

    public static string DefaultInstallFolder(string source) =>
        Path.Combine(Path.GetPathRoot(Path.GetFullPath(source)) ?? "C:\\", "ZHReforgedOnline");

    private static string? ReadFirstLine(string path)
    {
        try
        {
            return File.Exists(path) ? File.ReadLines(path).FirstOrDefault()?.Trim() : null;
        }
        catch (IOException)
        {
            return null;
        }
    }

    public static Task InstallAsync(string source, string target, IProgress<string> progress) =>
        Task.Run(() => Install(source, target, progress));

    private static void Install(string source, string target, IProgress<string> progress)
    {
        source = Path.GetFullPath(source).TrimEnd('\\');
        target = Path.GetFullPath(target).TrimEnd('\\');
        if (!Directory.Exists(PayloadFolder) || !File.Exists(Path.Combine(PayloadFolder, "generals.exe")))
            throw new InvalidOperationException("Le dossier « game » du launcher est introuvable ou incomplet.");
        if (string.Equals(source, target, StringComparison.OrdinalIgnoreCase) ||
            target.StartsWith(source + "\\", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Le dossier d'installation doit être en dehors du dossier de Zero Hour.");

        Directory.CreateDirectory(target);

        // 1. Zero Hour itself
        var work = new List<(string From, string To)>();
        Collect(source, source, target, work);

        // 2. the base game, when Zero Hour's folder does not carry it in ZH_Generals
        if (!File.Exists(Path.Combine(source, "ZH_Generals", "Textures.big")))
        {
            var parent = Path.GetDirectoryName(source);
            var baseGame = parent == null ? null : BaseGameSiblings.Select(name => Path.Combine(parent, name))
                .FirstOrDefault(folder => File.Exists(Path.Combine(folder, "Textures.big")));
            if (baseGame != null)
            {
                foreach (var big in Directory.EnumerateFiles(baseGame, "*.big"))
                    work.Add((big, Path.Combine(target, "ZH_Generals", Path.GetFileName(big))));
            }
        }

        int done = 0;
        foreach (var (from, to) in work)
        {
            if (++done % 25 == 0 || done == work.Count)
                progress.Report($"Fichiers du jeu : {done} / {work.Count}");
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            Place(from, to);
        }

        // 3. this build over it
        var payload = Directory.EnumerateFiles(PayloadFolder, "*", SearchOption.AllDirectories)
            .Where(file => !string.Equals(Path.GetFileName(file), VersionFile, StringComparison.OrdinalIgnoreCase))
            .ToList();
        done = 0;
        foreach (var file in payload)
        {
            if (++done % 25 == 0 || done == payload.Count)
                progress.Report($"Version en ligne : {done} / {payload.Count}");
            var to = Path.Combine(target, Path.GetRelativePath(PayloadFolder, file));
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            if (File.Exists(to))
                File.Delete(to); // a link to the original goes, the original stays
            File.Copy(file, to);
        }

        File.WriteAllText(Path.Combine(target, InstalledMarker), (PayloadVersion() ?? "inconnue") + Environment.NewLine);
        progress.Report("Installation terminée");
    }

    private static void Collect(string root, string folder, string target, List<(string, string)> work)
    {
        foreach (var file in Directory.EnumerateFiles(folder))
        {
            var name = Path.GetFileName(file);
            if (SkippedFiles.Contains(name) || name.EndsWith(".log", StringComparison.OrdinalIgnoreCase))
                continue;
            work.Add((file, Path.Combine(target, Path.GetRelativePath(root, file))));
        }
        foreach (var sub in Directory.EnumerateDirectories(folder))
        {
            if (SkippedFolders.Contains(Path.GetFileName(sub)))
                continue;
            Collect(root, sub, target, work);
        }
    }

    private static void Place(string from, string to)
    {
        var info = new FileInfo(from);
        if (LinkedExtensions.Contains(info.Extension))
        {
            if (File.Exists(to) && new FileInfo(to).Length == info.Length)
                return;
            if (File.Exists(to))
                File.Delete(to);
            if (CreateHardLink(to, from, IntPtr.Zero))
                return;
            File.Copy(from, to); // another drive: a copy then
            return;
        }

        if (File.Exists(to))
        {
            var existing = new FileInfo(to);
            if (existing.Length == info.Length && existing.LastWriteTimeUtc == info.LastWriteTimeUtc)
                return;
            File.Delete(to);
        }
        File.Copy(from, to);
    }
}
