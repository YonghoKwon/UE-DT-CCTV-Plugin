#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}
THIRD_PARTY_INCLUDES_END
