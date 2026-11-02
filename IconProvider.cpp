#include "IconProvider.h"

#include <QApplication>
#include <QHash>

static QStringList specialCandidates(const QString &b) {
    static const QHash<QString, QStringList> exact = {
        {"bash", {"utilities-terminal"}}, {"sh", {"utilities-terminal"}}, {"zsh", {"utilities-terminal"}},
        {"fish", {"utilities-terminal"}}, {"dash", {"utilities-terminal"}}, {"tmux", {"utilities-terminal"}},
        {"gnome-terminal-server", {"utilities-terminal", "org.gnome.Terminal"}},
        {"konsole", {"utilities-terminal", "konsole"}}, {"xterm", {"utilities-terminal", "xterm"}},
        {"sshd", {"network-server", "utilities-terminal"}}, {"ssh", {"network-server", "utilities-terminal"}},
        {"gnome-shell", {"user-desktop", "org.gnome.Shell", "gnome-shell"}},
        {"xorg", {"video-display", "video-x-generic"}}, {"xwayland", {"video-display", "video-x-generic"}},
        {"networkmanager", {"network-wired", "preferences-system-network"}},
        {"wpa_supplicant", {"network-wireless", "preferences-system-network"}},
        {"pulseaudio", {"audio-card", "audio-volume-high"}}, {"wireplumber", {"audio-card"}},
        {"cron", {"appointment-soon", "preferences-system-time"}}, {"atd", {"appointment-soon"}},
        {"dbus-daemon", {"preferences-system"}}, {"dbus-broker", {"preferences-system"}},
        {"cupsd", {"printer"}}, {"snapd", {"system-software-install", "snap-store"}},
        {"bluetoothd", {"bluetooth", "preferences-bluetooth"}},
        {"firefox", {"firefox", "firefox-esr", "web-browser"}}, {"firefox-bin", {"firefox", "web-browser"}},
        {"chrome", {"google-chrome", "chromium", "web-browser"}}, {"chromium", {"chromium", "web-browser"}},
        {"chromium-browser", {"chromium-browser", "chromium", "web-browser"}},
        {"code", {"com.visualstudio.code", "vscode", "code", "text-editor"}},
        {"thunderbird", {"thunderbird", "internet-mail"}}, {"nautilus", {"system-file-manager", "org.gnome.Nautilus"}},
        {"vlc", {"vlc", "multimedia-video-player"}}, {"spotify", {"spotify-client", "spotify"}},
        {"discord", {"discord"}}, {"telegram-desktop", {"telegram", "telegram-desktop"}},
        {"node", {"nodejs", "text-x-script"}}, {"java", {"java", "application-x-java"}},
        {"snap", {"system-software-install"}}, {"qtcreator", {"QtProject-qtcreator", "qtcreator"}},
    };
    auto it = exact.constFind(b);
    if (it != exact.constEnd()) return it.value();
    if (b.startsWith("python")) return {"text-x-python", "python", "text-x-script"};
    if (b.startsWith("systemd")) return {"system-run", "preferences-system"};
    if (b.startsWith("pipewire")) return {"audio-card", "audio-volume-high"};
    if (b.startsWith("gnome-")) return {"preferences-desktop", "org.gnome.Settings"};
    if (b.startsWith("gvfs")) return {"folder-remote", "system-file-manager"};
    return {};
}

QIcon IconProvider::cached(const QString &key, const QStringList &themeNames, QStyle::StandardPixmap fallback) {
    auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) return it.value();

    QIcon icon;
    for (const QString &n : themeNames) {
        if (!n.isEmpty() && QIcon::hasThemeIcon(n)) {
            icon = QIcon::fromTheme(n);
            if (!icon.isNull()) break;
        }
    }
    if (icon.isNull()) icon = QApplication::style()->standardIcon(fallback);
    m_cache.insert(key, icon);
    return icon;
}

QIcon IconProvider::forExe(const QString &exeBase, const QString &commFallback) {
    QStringList names = specialCandidates(exeBase);
    if (!exeBase.isEmpty()) names << exeBase;
    QString comm = commFallback;
    comm.replace(' ', '-');
    if (exeBase.isEmpty() && !comm.isEmpty()) names << comm;
    names << "application-x-executable";
    return cached("exe:" + (exeBase.isEmpty() ? comm : exeBase), names, QStyle::SP_FileIcon);
}

QIcon IconProvider::iconFor(const ProcessInfo &p) {
    if (p.state == 'Z') return cached("zombie", {"dialog-warning"}, QStyle::SP_MessageBoxWarning);

    const QString cmd = QString::fromUtf8(p.cmdline);
    if (cmd.startsWith('['))                               // kernel thread: cmdline dạng "[kworker/0:1]"
        return cached("kernel", {"cpu", "preferences-system", "applications-system"}, QStyle::SP_ComputerIcon);

    QString first = cmd.section(' ', 0, 0);                // token đầu = đường dẫn file thực thi
    QString base = first.section('/', -1).toLower();
    while (base.endsWith(':')) base.chop(1);               // "sshd:" -> "sshd"
    return forExe(base, QString::fromUtf8(p.name).toLower());
}

QIcon IconProvider::iconForName(const QString &comm) {
    return forExe(comm.toLower(), comm.toLower());
}
