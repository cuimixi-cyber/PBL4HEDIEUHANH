#ifndef ICONPROVIDER_H
#define ICONPROVIDER_H

#include "core_bridge.h"

#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QStyle>

/* IconProvider: chọn icon HỆ THỐNG mặc định (theme icon của desktop đang dùng, qua
 * QIcon::fromTheme) cho từng tiến trình.
 *
 * Icon chọn theo FILE THỰC THI (token đầu của cmdline), không theo tên comm, vì các tiến
 * trình con của Firefox có comm là "Web Content"/"Isolated Web Co" nhưng cùng file thực thi
 * `firefox` -> tất cả cùng hiện icon Firefox. Nếu theme không có icon thì dùng icon chuẩn của
 * Qt (QStyle::StandardPixmap) làm dự phòng, nên luôn có icon, không bao giờ trống. */
class IconProvider {
public:
    QIcon iconFor(const ProcessInfo &p);
    QIcon iconForName(const QString &comm);

private:
    QIcon cached(const QString &key, const QStringList &themeNames, QStyle::StandardPixmap fallback);
    QIcon forExe(const QString &exeBase, const QString &commFallback);
    QHash<QString, QIcon> m_cache;
};

#endif
