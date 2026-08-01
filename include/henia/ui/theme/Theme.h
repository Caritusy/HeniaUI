#pragma once

#include "henia/ui/Types.h"

namespace henia::ui {

struct Theme final {
    Color canvas{0.018F, 0.027F, 0.043F, 1.0F};
    Color surface{0.032F, 0.047F, 0.071F, 1.0F};
    Color surfaceRaised{0.046F, 0.064F, 0.092F, 1.0F};
    Color surfaceHover{0.060F, 0.092F, 0.125F, 1.0F};
    Color surfacePressed{0.075F, 0.125F, 0.165F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    Color accent{0.10F, 0.72F, 0.91F, 1.0F};
    Color accentStrong{0.06F, 0.58F, 0.82F, 1.0F};
    Color textPrimary{0.90F, 0.95F, 0.98F, 1.0F};
    Color textMuted{0.48F, 0.59F, 0.67F, 1.0F};
    Color danger{0.93F, 0.31F, 0.36F, 1.0F};
    float cornerRadius = 8.0F;
    float controlHeight = 36.0F;
};

} // namespace henia::ui
