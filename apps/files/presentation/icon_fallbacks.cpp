#include "icon_fallbacks.h"

bool IconFallbacks::isUnresolved(const QString& chain) const { return unresolved_.contains(chain); }

void IconFallbacks::markUnresolved(const QString& chain) { unresolved_.insert(chain); }
