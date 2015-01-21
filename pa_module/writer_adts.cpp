#include "writer_adts.h"

#include <QDebug>
#include <QDir>

#include "constants.h"

namespace {
const int kHlsTimePerSegmentS = 1;
const int kHlsListSize = 3;
const int kHlsWrap = kHlsListSize*2 + 2;
const char kHlsTsFilenameTemplate[] = "pacc_%03d.ts";
} // namespace


static AVDictionary *format_options() {
    // Sets the segment format options.
    AVDictionary *options = NULL;
    int ret;
    ret = av_dict_set(&options,"segment_format", "adts", 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set(&options, "segment_list_type", "hls", 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set(&options, "segment_list", QString("%1%2").arg(
                          Stream::kOutPath, Stream::kPlaylistFilename)
                      .toStdString().data(), 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set_int(&options, "segment_time", kHlsTimePerSegmentS, 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set(&options, "segment_list_flags", "live", 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set_int(&options, "segment_list_size", kHlsListSize, 0);
    Q_ASSERT(ret >= 0 && "Could not set property");
    ret = av_dict_set_int(&options, "segment_wrap", kHlsWrap, 0);
    Q_ASSERT(ret >= 0 && "Could not set property");

    return options;
}

ADTSWriter::ADTSWriter()
    : BaseWriter("segment", QString(Stream::kOutPath) + kHlsTsFilenameTemplate,
                 AV_CODEC_ID_AAC, format_options())
{
}
