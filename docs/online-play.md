# Jouer en ligne (pair à pair)

La partie LAN de Zero Hour Reforged jouée par internet, sans serveur ni VPN (Radmin, Hamachi…) :
un joueur héberge, les autres le rejoignent avec un code de partie. Tout se passe dans le jeu et
dans le launcher « Zero Hour Reforged Online ».

Branche : `feat/p2p-online` du fork. Releases : `online-<commit>` sur
https://github.com/Robocnop/CnCGeneralsZH-Reforged/releases

---

## Pour les joueurs

### Installer

1. Télécharger `ZHReforgedOnline.exe` depuis la dernière release `online-…`.
2. Le lancer. Il trouve Zero Hour tout seul (dossier du launcher officiel, puis bibliothèques Steam) ;
   sinon, le bouton jaune demande le dossier.
3. Cliquer sur **Installer**. Le launcher télécharge le jeu (`ZHReforgedOnline-game.zip`, ~16 Mo) et
   l'installe dans `<lecteur de Zero Hour>:\ZHReforgedOnline`.

L'installation Steam et celle du launcher officiel ne sont jamais modifiées. Les gros fichiers
(`.big`, `.bik`) sont des liens physiques vers ceux de Zero Hour quand les deux dossiers sont sur le
même disque : ils ne prennent pas de place en plus.

Quand une nouvelle release sort, le launcher affiche **Mise à jour disponible**. Tout le monde doit
avoir la même version (affichée sous « JEU ») pour jouer ensemble.

### Jouer

1. Chacun lit son **code de partie** dans le launcher (ex. `BETJ-H453`) et l'envoie à l'autre.
2. **L'hôte** colle le code de son ami dans « Code de l'ami », clique sur **Héberger en ligne**, puis
   sur **Créer partie** dans le jeu.
3. **L'ami** colle le code de l'hôte dans « Code de l'ami » et clique sur **Rejoindre** : le jeu le
   connecte tout seul (il réessaie pendant 20 secondes).

Sans le launcher : le jeu affiche le code sur l'écran Connexion directe. L'hôte tape le code de son
ami dans « IP distant » avant **Créer partie** ; l'ami tape le code de l'hôte et clique sur **Rejoindre**.
Une adresse IP marche partout où un code marche.

### Si ça ne marche pas

- **Pare-feu Windows** : accepter la fenêtre au premier lancement, en cochant Public et Privé. Ou, dans
  un terminal administrateur :
  `New-NetFirewallRule -DisplayName "Zero Hour Reforged Online" -Direction Inbound -Program "D:\ZHReforgedOnline\generals.exe" -Action Allow -Profile Any`
- **Le code affiché dans le jeu est suivi d'une adresse en 192.168…** : le jeu n'a pas trouvé l'adresse
  publique. Il essaie déjà toutes les cartes réseau ; sinon, régler la bonne carte dans les options
  (`IPAddress` et `GameSpyIPAddress` de `Options.ini`). Les cartes VMware, VirtualBox et VPN sont les
  coupables habituelles.
- **L'ami n'arrive toujours pas à rejoindre** : ouvrir à la main UDP **8086** et **8088** vers le PC de
  l'hôte dans la box (menu NAT/PAT), ou activer l'UPnP.
- **Les logs** : `DebugLogFile.txt` dans le dossier d'installation, à récupérer avant de relancer le
  jeu (il est remplacé à chaque lancement). Les lignes utiles :

  | Ligne | Signification |
  |---|---|
  | `stunQueryPublicIP - … sees us at a.b.c.d` | adresse publique trouvée |
  | `applyPublicIP - … leads nowhere, … reaches the internet` | mauvaise carte réseau évitée |
  | `UPnPPortMapper - UDP 8086 passes to …` | la box a accepté d'ouvrir le port |
  | `addPunchTarget - probing …` | l'hôte envoie des sondes à cet ami |
  | `RequestGameJoinDirectConnect - asking …` | l'invité demande à rejoindre cette adresse |
  | `talks to the lobby from port …` | un joueur est arrivé jusqu'au salon |
  | `learnPeerPort - player … is at …` | en partie, le vrai port d'un joueur est appris |

### Limites connues

- Deux joueurs derrière la même box (même adresse publique) ne peuvent pas jouer ensemble ainsi.
- Le hole punching suppose que les box gardent le port d'origine des paquets, ce que font la plupart
  des box de particuliers. Derrière une box qui ne le fait pas (certaines connexions 4G/5G, IP
  partagée par l'opérateur), il faut ouvrir les ports chez l'hôte.
- Si l'hôte quitte, le joueur suivant devient routeur ; si les autres ne peuvent pas le joindre, c'est
  l'écran de déconnexion habituel.
- Le chat du salon entre deux invités peut ne pas passer (seuls les messages via l'hôte sont garantis).

---

## Comment ça marche

### Identité publique (STUN)

En LAN, chaque joueur est connu par l'adresse de sa machine. Avec `-publicip auto`, le salon demande à
un serveur STUN public (`stun.l.google.com:19302`, `stun.cloudflare.com:3478`) l'adresse sous laquelle
internet le voit, et se fait connaître sous celle-là. Les sockets restent sur l'adresse locale :
`LANAPI::m_localIP` est l'identité, `m_bindIP` l'adresse d'écoute. Si la carte choisie ne mène pas à
internet, les autres adresses de la machine sont essayées (`LANAPI::applyPublicIP`).

### Ports de l'hôte (UPnP)

L'hôte demande à sa box, par UPnP IGD, de lui faire suivre UDP 8086 (salon) et 8088 (partie), avec un
bail de 4 h, retirés à la fermeture du jeu (`UPnPPortMapper`).

### Ports des invités (apprentissage)

La box d'un invité choisit elle-même le port extérieur de ses paquets. L'hôte note le port d'où vient
chaque joueur et lui répond là : dans le salon (`LANAPI::m_lobbyPorts`, `lobbyPortFor`) et en partie
(`ConnectionManager::learnPeerPort`). Un invité n'a donc rien à ouvrir.

### Hole punching

Une box laisse entrer ce qui ressemble à la réponse à un paquet sorti. L'hôte envoie donc une sonde
deux fois par seconde aux amis qu'il attend (`-punch`, ou le code tapé avant Créer partie), et l'invité
sonde l'hôte en redemandant à rejoindre chaque seconde (`LANAPI::punchHoles`). En partie, les deux
côtés s'envoient leurs images dès le départ, ce qui ouvre le port 8088 de la même façon.

### Relais par l'hôte

La plupart des commandes passent par le routeur de paquets (l'hôte). Certaines vont directement d'un
joueur à l'autre : votes de déconnexion, fin de chargement, renvois d'images, transfert de carte. Deux
invités qui ne se joignent pas les font passer par l'hôte, au niveau du transport UDP
(`RelayEnvelope.h`, `Transport`), sans que le reste du jeu le sache :

| Datagramme | Contenu |
|---|---|
| `ZHRF` ip port datagramme | vers le routeur : « envoie ceci à ip:port » |
| `ZHRD` ip port datagramme | du routeur : « ceci vient de ip:port » |
| `ZHRP` entendu 0 0 0 | sonde ; `entendu` = 1 si l'émetteur nous reçoit directement |

Chaque paire s'envoie des sondes ; un joueur passe par le routeur pour un pair qui n'a pas dit l'entendre
depuis 3 s, et revient en direct dès qu'il le dit. Le routeur ne relaie qu'entre deux joueurs de la
partie. Rien de tout cela n'est actif sans `-publicip` : une partie LAN est inchangée.

### Code de partie

8 caractères en base32 de Crockford (`XXXX-XXXX`) : les 4 octets de l'adresse IPv4 et un CRC-8
(polynôme 0x07). Lu sans tenir compte de la casse, des tirets ni des espaces ; O vaut 0, I et L valent 1.
Une faute de frappe est refusée. Même algorithme dans le jeu (`GameCode.cpp`) et le launcher
(`GameCode.cs`).

### Options de ligne de commande

| Option | Effet |
|---|---|
| `-publicip auto` / `-publicip a.b.c.d` | jouer le salon LAN par internet (STUN, UPnP, relais) |
| `-directconnect` | ouvrir l'écran Connexion directe au démarrage |
| `-connect <code\|ip>` | rejoindre cet hôte dès que l'écran est ouvert |
| `-punch <code\|ip>[,…]` | sonder ces joueurs pour que leurs demandes passent la box de l'hôte |
| `-language french` | jouer en français |

Le launcher lance : `generals.exe -rk7 -language <langue> -publicip auto -directconnect [-punch <code>]`
pour héberger, `… -publicip auto -connect <code>` pour rejoindre.

---

## Le launcher (`Tools/OnlineLauncher`)

Application WPF .NET 8 autonome, en français, au style du launcher officiel.

| Fichier | Rôle |
|---|---|
| `MainWindow.xaml(.cs)` | fenêtre, états (introuvable, à installer, à mettre à jour, prêt), lancement du jeu |
| `Installer.cs` | installation à côté de Zero Hour : liens physiques pour `.big`/`.bik`, copie du reste, puis la version en ligne par-dessus |
| `Updater.cs` | dernière release `online-<commit>` via l'API GitHub, téléchargement et décompression dans `%LOCALAPPDATA%\ZHReforgedOnline\payload` |
| `Stun.cs`, `GameCode.cs` | adresse publique et code de partie, comme dans le jeu |
| `Settings.cs` | réglages dans `%APPDATA%\ZHReforgedOnline\settings.json`, recherche de Zero Hour |
| `pack.ps1` | construit le launcher et le paquet du jeu |

### Publier une nouvelle version

1. Construire le jeu : `build.bat Release test` (avec CMake dans le PATH).
2. Commiter, puis `Tools\OnlineLauncher\pack.ps1`. Il produit
   `dist\ZHReforgedOnline\ZHReforgedOnline.exe` et `dist\ZHReforgedOnline-game.zip`, versionnés
   `v<tag>+<commit>`.
3. Publier la release avec le tag qu'il affiche :

   ```
   gh release create online-<commit> -R Robocnop/CnCGeneralsZH-Reforged --target <sha complet> --title "Zero Hour Reforged en ligne (<commit>)" --notes "…" Tools\OnlineLauncher\dist\ZHReforgedOnline\ZHReforgedOnline.exe Tools\OnlineLauncher\dist\ZHReforgedOnline-game.zip
   ```

Les launchers déjà installés proposent alors la mise à jour du jeu. Un changement du launcher lui-même
demande de re-télécharger l'exe.

---

## Fichiers du jeu concernés

| Fichier | Rôle |
|---|---|
| `GameNetwork/StunClient.*` | requête STUN |
| `GameNetwork/UPnPPortMapper.*` | ouverture des ports de la box |
| `GameNetwork/RelayEnvelope.*` | format du relais et des sondes |
| `GameNetwork/GameCode.*` | code de partie |
| `GameNetwork/Transport.*` | relais, sondes, `sendProbe` |
| `GameNetwork/LANAPI.*` | identité publique, ports du salon, hole punching |
| `GameNetwork/ConnectionManager.*` | routes du relais, ports appris en partie |
| `GUI/.../NetworkDirectConnect.cpp` | affichage et saisie du code |
| `Common/CommandLine.cpp`, `GlobalData.*` | options ci-dessus |

Tests : `test_gameengine` (STUN, UPnP, enveloppes de relais, sondes, code de partie, options).
