/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportMP2.cpp

  Joshua Haberman
  Markus Meyer

  Copyright 2002, 2003 Joshua Haberman.
  Copyright 2006 Markus Meyer
  Some portions may be Copyright 2003 Paolo Patruno.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*******************************************************************//**

\class MP2Exporter
\brief Class used to export MP2 files

*/



#ifdef USE_LIBTWOLAME

#include <wx/defs.h>
#include <wx/dynlib.h>
#include <wx/log.h>
#include <wx/stream.h>

#include "Export.h"
#include "FileIO.h"
#include "Mix.h"
#include "ProjectRate.h"
#include "Tags.h"
#include "Track.h"

#include "ExportUtils.h"
#include "ExportProgressListener.h"
#include "PlainExportOptionsEditor.h"

#define LIBTWOLAME_STATIC
#include "twolame.h"

#ifdef USE_LIBID3TAG
   #include <id3tag.h>
   // DM: the following functions were supposed to have been
   // included in id3tag.h - should be fixed in the next release
   // of mad.
   extern "C" {
      struct id3_frame *id3_frame_new(char const *);
      id3_length_t id3_latin1_length(id3_latin1_t const *);
      void id3_latin1_decode(id3_latin1_t const *, id3_ucs4_t *);
   }
#endif

//----------------------------------------------------------------------------
// ExportMP2Options
//----------------------------------------------------------------------------

namespace {

// i18n-hint kbps abbreviates "thousands of bits per second"
inline TranslatableString n_kbps( int n ) { return XO("%d kbps").Format( n ); }

const TranslatableStrings BitRateNames {
   n_kbps(16),
   n_kbps(24),
   n_kbps(32),
   n_kbps(40),
   n_kbps(48),
   n_kbps(56),
   n_kbps(64),
   n_kbps(80),
   n_kbps(96),
   n_kbps(112),
   n_kbps(128),
   n_kbps(160),
   n_kbps(192),
   n_kbps(224),
   n_kbps(256),
   n_kbps(320),
   n_kbps(384),
};

enum : int {
   MP2OptionIDBitRate = 0
};

const std::initializer_list<PlainExportOptionsEditor::OptionDesc> MP2Options {
   {
      {
         MP2OptionIDBitRate, XO("Bit Rate"),
         160,
         ExportOption::TypeEnum,
         {
            16,
            24,
            32,
            40,
            48,
            56,
            64,
            80,
            96,
            112,
            128,
            160,
            192,
            224,
            256,
            320,
            384,
         }, BitRateNames
      },
      wxT("/FileFormats/MP2Bitrate")
   }
};

}

class ExportMP2 final : public ExportPluginEx
{
public:

   ExportMP2();

   int GetFormatCount() const override;
   FormatInfo GetFormatInfo(int) const override;
   
   // Required

   std::unique_ptr<ExportOptionsEditor>
   CreateOptionsEditor(int, ExportOptionsEditor::Listener*) const override;
   
   void Export(AudacityProject *project,
               ExportProgressListener &progressListener,
               const Parameters& parameters,
               unsigned channels,
               const wxFileNameWrapper &fName,
               bool selectedOnly,
               double t0,
               double t1,

               MixerSpec *mixerSpec,
               const Tags *metadata,
               int subformat) override;
private:

   int AddTags(AudacityProject *project, ArrayOf<char> &buffer, bool *endOfFile, const Tags *tags);
#ifdef USE_LIBID3TAG
   void AddFrame(struct id3_tag *tp, const wxString & n, const wxString & v, const char *name);
#endif

};

ExportMP2::ExportMP2() = default;

int ExportMP2::GetFormatCount() const
{
   return 1;
}

FormatInfo ExportMP2::GetFormatInfo(int) const
{
   return {
      wxT("MP2"), XO("MP2 Files"), { wxT("mp2") }, 2, true
   };
}

std::unique_ptr<ExportOptionsEditor>
ExportMP2::CreateOptionsEditor(int, ExportOptionsEditor::Listener*) const
{
   return std::make_unique<PlainExportOptionsEditor>(MP2Options);
}


void ExportMP2::Export(AudacityProject *project,
   ExportProgressListener &progressListener, const Parameters& parameters,
   unsigned channels, const wxFileNameWrapper &fName,
   bool selectionOnly, double t0, double t1, MixerSpec *mixerSpec, const Tags *metadata,
   int)
{
   ExportBegin();
   
   bool stereo = (channels == 2);
   long bitrate = ExportUtils::GetParameterValue(parameters, MP2OptionIDBitRate, 160);
   double rate = ProjectRate::Get(*project).GetRate();
   const auto &tracks = TrackList::Get( *project );

   wxLogNull logNo;             /* temporarily disable wxWidgets error messages */

   twolame_options *encodeOptions;
   encodeOptions = twolame_init();
   auto cleanup = finally( [&] { twolame_close(&encodeOptions); } );

   twolame_set_in_samplerate(encodeOptions, (int)(rate + 0.5));
   twolame_set_out_samplerate(encodeOptions, (int)(rate + 0.5));
   twolame_set_bitrate(encodeOptions, bitrate);
   twolame_set_num_channels(encodeOptions, stereo ? 2 : 1);

   if (twolame_init_params(encodeOptions) != 0)
   {
      SetErrorString(XO("Cannot export MP2 with this sample rate and bit rate"));
      progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
      return;
   }

   // Put ID3 tags at beginning of file
   if (metadata == NULL)
      metadata = &Tags::Get( *project );

   FileIO outFile(fName, FileIO::Output);
   if (!outFile.IsOpened()) {
      SetErrorString(XO("Unable to open target file for writing"));
      progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
      return;
   }

   ArrayOf<char> id3buffer;
   int id3len;
   bool endOfFile;
   id3len = AddTags(project, id3buffer, &endOfFile, metadata);
   if (id3len && !endOfFile) {
      if ( outFile.Write(id3buffer.get(), id3len).GetLastError() ) {
         // TODO: more precise message
         ShowExportErrorDialog("MP2:292");
         progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
         return;
      }
   }

   // Values taken from the twolame simple encoder sample
   const size_t pcmBufferSize = 9216 / 2; // number of samples
   const size_t mp2BufferSize = 16384u; // bytes

   // We allocate a buffer which is twice as big as the
   // input buffer, which should always be enough.
   // We have to multiply by 4 because one sample is 2 bytes wide!
   ArrayOf<unsigned char> mp2Buffer{ mp2BufferSize };

   bool hasError {false};
   
   {
      auto mixer = ExportUtils::CreateMixer(tracks, selectionOnly,
         t0, t1,
         stereo ? 2 : 1, pcmBufferSize, true,
         rate, int16Sample, mixerSpec);

      SetStatusString(selectionOnly
         ? XO("Exporting selected audio at %ld kbps")
              .Format( bitrate )
         : XO("Exporting the audio at %ld kbps")
              .Format( bitrate ));
      
      progressListener.OnExportProgress(0);

      while (!IsCancelled() && !IsStopped()) {
         auto pcmNumSamples = mixer->Process();
         if (pcmNumSamples == 0)
            break;

         short *pcmBuffer = (short *)mixer->GetBuffer();

         int mp2BufferNumBytes = twolame_encode_buffer_interleaved(
            encodeOptions,
            pcmBuffer,
            pcmNumSamples,
            mp2Buffer.get(),
            mp2BufferSize);

         if (mp2BufferNumBytes < 0) {
            // TODO: more precise message
            ShowExportErrorDialog("MP2:339");
            hasError = true;
            break;
         }

         if ( outFile.Write(mp2Buffer.get(), mp2BufferNumBytes).GetLastError() ) {
            // TODO: more precise message
            ShowDiskFullExportErrorDialog(fName);
            progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
            return;
         }
         progressListener.OnExportProgress(ExportUtils::EvalExportProgress(*mixer, t0, t1));
      }
   }

   int mp2BufferNumBytes = twolame_encode_flush(
      encodeOptions,
      mp2Buffer.get(),
      mp2BufferSize);

   if (mp2BufferNumBytes > 0)
      if ( outFile.Write(mp2Buffer.get(), mp2BufferNumBytes).GetLastError() ) {
         // TODO: more precise message
         ShowExportErrorDialog("MP2:362");
         progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
         return;
      }

   /* Write ID3 tag if it was supposed to be at the end of the file */

   if (id3len && endOfFile)
      if ( outFile.Write(id3buffer.get(), id3len).GetLastError() ) {
         // TODO: more precise message
         ShowExportErrorDialog("MP2:371");
         progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
         return;
      }

   if ( !outFile.Close() ) {
      // TODO: more precise message
      ShowExportErrorDialog("MP2:377");
      progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
      return;
   }

   if(hasError)
      progressListener.OnExportResult(ExportProgressListener::ExportResult::Error);
   else
      ExportFinish(progressListener);
}

#ifdef USE_LIBID3TAG
struct id3_tag_deleter {
   void operator () (id3_tag *p) const { if (p) id3_tag_delete(p); }
};
using id3_tag_holder = std::unique_ptr<id3_tag, id3_tag_deleter>;
#endif

// returns buffer len; caller frees
int ExportMP2::AddTags(
   AudacityProject * WXUNUSED(project), ArrayOf< char > &buffer,
   bool *endOfFile, const Tags *tags)
{
#ifdef USE_LIBID3TAG
   id3_tag_holder tp { id3_tag_new() };

   for (const auto &pair : tags->GetRange()) {
      const auto &n = pair.first;
      const auto &v = pair.second;
      const char *name = "TXXX";

      if (n.CmpNoCase(TAG_TITLE) == 0) {
         name = ID3_FRAME_TITLE;
      }
      else if (n.CmpNoCase(TAG_ARTIST) == 0) {
         name = ID3_FRAME_ARTIST;
      }
      else if (n.CmpNoCase(TAG_ALBUM) == 0) {
         name = ID3_FRAME_ALBUM;
      }
      else if (n.CmpNoCase(TAG_YEAR) == 0) {
         // LLL:  Some apps do not like the newer frame ID (ID3_FRAME_YEAR),
         //       so we add old one as well.
         AddFrame(tp.get(), n, v, "TYER");
         name = ID3_FRAME_YEAR;
      }
      else if (n.CmpNoCase(TAG_GENRE) == 0) {
         name = ID3_FRAME_GENRE;
      }
      else if (n.CmpNoCase(TAG_COMMENTS) == 0) {
         name = ID3_FRAME_COMMENT;
      }
      else if (n.CmpNoCase(TAG_TRACK) == 0) {
         name = ID3_FRAME_TRACK;
      }

      AddFrame(tp.get(), n, v, name);
   }

   tp->options &= (~ID3_TAG_OPTION_COMPRESSION); // No compression

   // If this version of libid3tag supports it, use v2.3 ID3
   // tags instead of the newer, but less well supported, v2.4
   // that libid3tag uses by default.
   #ifdef ID3_TAG_HAS_TAG_OPTION_ID3V2_3
   tp->options |= ID3_TAG_OPTION_ID3V2_3;
   #endif

   *endOfFile = false;

   id3_length_t len;

   len = id3_tag_render(tp.get(), 0);
   buffer.reinit(len);
   len = id3_tag_render(tp.get(), (id3_byte_t *)buffer.get());


   return len;
#else //ifdef USE_LIBID3TAG
   return 0;
#endif
}

#ifdef USE_LIBID3TAG
void ExportMP2::AddFrame(struct id3_tag *tp, const wxString & n, const wxString & v, const char *name)
{
   struct id3_frame *frame = id3_frame_new(name);

   if (!n.IsAscii() || !v.IsAscii()) {
      id3_field_settextencoding(id3_frame_field(frame, 0), ID3_FIELD_TEXTENCODING_UTF_16);
   }
   else {
      id3_field_settextencoding(id3_frame_field(frame, 0), ID3_FIELD_TEXTENCODING_ISO_8859_1);
   }

   MallocString<id3_ucs4_t> ucs4 {
      id3_utf8_ucs4duplicate((id3_utf8_t *) (const char *) v.mb_str(wxConvUTF8)) };

   if (strcmp(name, ID3_FRAME_COMMENT) == 0) {
      // A hack to get around iTunes not recognizing the comment.  The
      // language defaults to XXX and, since it's not a valid language,
      // iTunes just ignores the tag.  So, either set it to a valid language
      // (which one???) or just clear it.  Unfortunately, there's no supported
      // way of clearing the field, so do it directly.
      id3_field *f = id3_frame_field(frame, 1);
      memset(f->immediate.value, 0, sizeof(f->immediate.value));
      id3_field_setfullstring(id3_frame_field(frame, 3), ucs4.get());
   }
   else if (strcmp(name, "TXXX") == 0) {
      id3_field_setstring(id3_frame_field(frame, 2), ucs4.get());

      ucs4.reset(id3_utf8_ucs4duplicate((id3_utf8_t *) (const char *) n.mb_str(wxConvUTF8)));

      id3_field_setstring(id3_frame_field(frame, 1), ucs4.get());
   }
   else {
      auto addr = ucs4.get();
      id3_field_setstrings(id3_frame_field(frame, 1), 1, &addr);
   }

   id3_tag_attachframe(tp, frame);
}
#endif

static Exporter::RegisteredExportPlugin sRegisteredPlugin{ "MP2",
   []{ return std::make_unique< ExportMP2 >(); }
};

#endif // #ifdef USE_LIBTWOLAME

