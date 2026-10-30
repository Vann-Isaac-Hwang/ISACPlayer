#pragma once

#include <QString>
#include <QStringList>

namespace isac::platform {

QString resolvePluginDirectory(const QStringList& candidates);
QString dspPluginDirectory();
QString formatPluginDirectory();

} // namespace isac::platform
