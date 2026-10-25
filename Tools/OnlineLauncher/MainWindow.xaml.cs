using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Imaging;

namespace ZHReforgedOnline;

public partial class MainWindow : Window
{
    private static readonly (string Id, string Name)[] Languages =
    {
        ("french", "Français"), ("english", "English"), ("german", "Deutsch"),
        ("turkish", "Türkçe"), ("ukrainian", "Українська"), ("russian", "Русский"),
    };

    private enum State { NoGame, NotInstalled, UpdateAvailable, Ready }

    private readonly Settings settings = Settings.Load();
    private uint? publicIP;
    private bool busy;

    public MainWindow()
    {
        InitializeComponent();
        var version = Assembly.GetExecutingAssembly().GetName().Version;
        LauncherVersion.Text = version == null ? "v1.0.0" : $"v{version.Major}.{version.Minor}.{version.Build}";
        FriendCode.Text = settings.FriendCode ?? "";
        Loaded += async (_, _) =>
        {
            Refresh();
            LoadArtwork();
            await LookUpCodeAsync();
        };
    }

    private string? SourceFolder
    {
        get
        {
            if (!Settings.IsZeroHourFolder(settings.SourceFolder))
            {
                settings.SourceFolder = Settings.FindZeroHour();
                if (settings.SourceFolder != null)
                    settings.Save();
            }
            return settings.SourceFolder;
        }
    }

    private string? InstallFolder =>
        settings.InstallFolder ?? (SourceFolder is { } source ? Installer.DefaultInstallFolder(source) : null);

    private State CurrentState()
    {
        if (SourceFolder == null)
            return State.NoGame;
        if (!Installer.IsInstalled(InstallFolder))
            return State.NotInstalled;
        var payload = Installer.PayloadVersion();
        if (payload != null && payload != Installer.InstalledVersion(InstallFolder))
            return State.UpdateAvailable;
        return State.Ready;
    }

    private void Refresh()
    {
        var state = CurrentState();
        var installed = Installer.InstalledVersion(InstallFolder);
        var payload = Installer.PayloadVersion();
        GameVersion.Text = installed ?? payload ?? "—";

        switch (state)
        {
            case State.NoGame:
                StatusTitle.Text = "Zero Hour introuvable";
                GameState.Text = "choisissez son dossier";
                PlayButton.Content = "Choisir le dossier de Zero Hour";
                break;
            case State.NotInstalled:
                StatusTitle.Text = "Installation requise";
                GameState.Text = "non installé";
                PlayButton.Content = "Installer";
                break;
            case State.UpdateAvailable:
                StatusTitle.Text = "Mise à jour disponible";
                GameState.Text = $"nouvelle version : {payload}";
                PlayButton.Content = "Mettre à jour";
                break;
            default:
                StatusTitle.Text = "Prêt à jouer";
                GameState.Text = "installé";
                PlayButton.Content = "Jouer";
                break;
        }

        bool ready = state == State.Ready;
        PlayButton.IsEnabled = !busy;
        HostButton.IsEnabled = ready && !busy;
        JoinButton.IsEnabled = ready && !busy && GameCode.IsJoinTarget(FriendCode.Text);
        ReinstallButton.IsEnabled = !busy && state != State.NoGame && Directory.Exists(Installer.PayloadFolder);
        var language = Languages.FirstOrDefault(l => l.Id == settings.Language);
        LanguageButton.Content = $"Langue : {language.Name ?? Languages[0].Name}";
    }

    private void LoadArtwork()
    {
        var candidates = new[] { InstallFolder, Installer.PayloadFolder, SourceFolder }
            .Where(folder => folder != null)
            .Select(folder => Path.Combine(folder!, "Install_Final.bmp"));
        var file = candidates.FirstOrDefault(File.Exists);
        if (file == null)
            return;
        try
        {
            var image = new BitmapImage();
            image.BeginInit();
            image.CacheOption = BitmapCacheOption.OnLoad;
            image.UriSource = new Uri(file);
            image.EndInit();
            Artwork.Source = image;
        }
        catch (Exception)
        {
            // no picture, the panel stays dark
        }
    }

    private async Task LookUpCodeAsync()
    {
        MyCode.Text = "…";
        MyCodeHint.Text = "Recherche de votre adresse publique…";
        publicIP = await Stun.QueryPublicIPAsync();
        if (publicIP is { } ip)
        {
            MyCode.Text = GameCode.FromIP(ip);
            MyCodeHint.Text = $"Adresse publique {GameCode.ToDotted(ip)}. Donnez ce code à vos amis, puis cliquez sur « Héberger en ligne ».";
            CopyButton.IsEnabled = true;
        }
        else
        {
            MyCode.Text = "indisponible";
            MyCodeHint.Text = "Aucun serveur STUN n'a répondu : vérifiez votre connexion internet.";
        }
    }

    private async Task InstallAsync()
    {
        var source = SourceFolder;
        var target = InstallFolder;
        if (source == null || target == null)
            return;

        busy = true;
        Refresh();
        StatusTitle.Text = "Installation…";
        try
        {
            var progress = new Progress<string>(text => StatusText.Text = text);
            await Installer.InstallAsync(source, target, progress);
            settings.InstallFolder = target;
            settings.Save();
            StatusText.Text = $"Installé dans {target}";
        }
        catch (Exception error)
        {
            StatusText.Text = "Échec de l'installation";
            MessageBox.Show(this, error.Message, "Installation", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            busy = false;
            Refresh();
            LoadArtwork();
        }
    }

    private void Launch(string extraArguments)
    {
        var folder = InstallFolder;
        if (folder == null)
            return;
        var exe = Path.Combine(folder, "generals.exe");
        var arguments = $"-rk7 -language {settings.Language} {extraArguments}".Trim();
        try
        {
            Process.Start(new ProcessStartInfo(exe, arguments) { WorkingDirectory = folder, UseShellExecute = false });
            settings.Save();
            WindowState = WindowState.Minimized;
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, "Lancement du jeu", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private bool ChooseSourceFolder()
    {
        var dialog = new Microsoft.Win32.OpenFolderDialog { Title = "Dossier de Command & Conquer Generals - Zero Hour" };
        if (dialog.ShowDialog(this) != true)
            return false;
        if (!Settings.IsZeroHourFolder(dialog.FolderName))
        {
            MessageBox.Show(this, "Ce dossier ne contient pas Zero Hour (INIZH.big est introuvable).", "Zero Hour",
                MessageBoxButton.OK, MessageBoxImage.Information);
            return false;
        }
        settings.SourceFolder = dialog.FolderName;
        settings.InstallFolder = null;
        settings.Save();
        return true;
    }

    private async void OnPlay(object sender, RoutedEventArgs e)
    {
        switch (CurrentState())
        {
            case State.NoGame:
                if (ChooseSourceFolder())
                    Refresh();
                break;
            case State.NotInstalled:
            case State.UpdateAvailable:
                await InstallAsync();
                break;
            default:
                Launch("");
                break;
        }
    }

    private void OnHost(object sender, RoutedEventArgs e) => Launch("-publicip auto -directconnect");

    private void OnJoin(object sender, RoutedEventArgs e)
    {
        var text = FriendCode.Text.Trim();
        var target = GameCode.TryToIP(text, out var ip) ? GameCode.FromIP(ip) : text;
        Launch($"-publicip auto -connect {target}");
    }

    private void OnFriendCodeChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        settings.FriendCode = FriendCode.Text;
        if (IsLoaded)
            Refresh();
    }

    private void OnCopyCode(object sender, RoutedEventArgs e)
    {
        try
        {
            Clipboard.SetText(MyCode.Text);
            StatusText.Text = "Code copié";
        }
        catch (Exception)
        {
            StatusText.Text = "Copie impossible";
        }
    }

    private async void OnReinstall(object sender, RoutedEventArgs e) => await InstallAsync();

    private void OnOpenFolder(object sender, RoutedEventArgs e)
    {
        var folder = new[] { InstallFolder, SourceFolder }.FirstOrDefault(f => f != null && Directory.Exists(f));
        if (folder != null)
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{folder}\"") { UseShellExecute = true });
    }

    private void OnLanguage(object sender, RoutedEventArgs e)
    {
        int index = Array.FindIndex(Languages, l => l.Id == settings.Language);
        settings.Language = Languages[(index + 1) % Languages.Length].Id;
        settings.Save();
        Refresh();
    }

    private void OnDragWindow(object sender, MouseButtonEventArgs e)
    {
        if (e.ButtonState == MouseButtonState.Pressed)
            DragMove();
    }

    private void OnMinimize(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnClose(object sender, RoutedEventArgs e)
    {
        settings.Save();
        Close();
    }
}
