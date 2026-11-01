#include "movieplayback.h"
#include "../video/ffmpegplayer.h"
#include "../io/physicalfilestream.h"
#include "../audio/openal/audiocommon.h"
extern "C" {
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <array>
#include <deque>
#include <limits>

namespace Impacto::Saekano {
namespace {
void CheckMovieAl(){if(alGetError()!=AL_NO_ERROR)throw FormatError("movie OpenAL operation failed");}
using VideoItem=Video::AVDecodedItem<AVMEDIA_TYPE_VIDEO>;
}
struct MoviePlayback::Impl {
  MoviePlaybackState State;
  std::unique_ptr<Video::FFmpegPlayer> Decoder;
  std::deque<VideoItem> Video;
  struct Pcm {std::vector<int16_t> Samples;size_t Offset=0;};
  std::deque<Pcm> PcmQueue;
  struct Buffer {ALuint Id;uint64_t Frames;};
  std::deque<Buffer> Queued;
  std::deque<ALuint> Free;
  std::array<ALuint,8> Buffers{};
  ALuint Source=0;SwrContext* Convert=nullptr;
  uint64_t PlayedBase=0,Converted=0,StartedAt=0;
  std::optional<uint64_t> TailAt;
  int64_t TailPosition=0,LastFrameEnd=0,FramePeriod=0;
  bool HasAudio=false;
  std::filesystem::path Path;
  std::optional<uint64_t> PausedAt;
  ~Impl(){Close();}
  void Close(){
    if(Source){alSourceStop(Source);alSourcei(Source,AL_BUFFER,0);alDeleteSources(1,&Source);Source=0;}
    if(Buffers[0]){alDeleteBuffers(ALsizei(Buffers.size()),Buffers.data());Buffers={};}
    if(Decoder){Decoder->Stop();Decoder.reset();}
    swr_free(&Convert);Video.clear();PcmQueue.clear();Queued.clear();Free.clear();
    State.Prepared=false;State.Started=false;State.Paused=false;PausedAt.reset();State.PendingAudio=State.PendingVideo=0;
  }
  void Reap(){
    if(!Source)return;
    ALint processed=0;alGetSourcei(Source,AL_BUFFERS_PROCESSED,&processed);
    while(processed-->0){
      if(Queued.empty())throw FormatError("movie device buffer accounting mismatch");
      ALuint id=0;alSourceUnqueueBuffers(Source,1,&id);const auto front=Queued.front();Queued.pop_front();
      if(id!=front.Id)throw FormatError("movie device buffer order mismatch");
      PlayedBase+=front.Frames;Free.push_back(id);
    }
    ALint offset=0;alGetSourcei(Source,AL_SAMPLE_OFFSET,&offset);
    State.AudioPlayed=PlayedBase+uint64_t(std::max(offset,0));CheckMovieAl();
  }
  void Fill(){
    while(!Free.empty() && State.PendingAudio && (State.PendingAudio>=4096 || State.AudioEof || !State.Started)){
      const size_t frames=size_t(std::min(uint64_t(4096),State.PendingAudio));std::vector<int16_t> pcm(frames*2);size_t copied=0;
      while(copied<pcm.size()){
        auto& block=PcmQueue.front();const auto count=std::min(pcm.size()-copied,block.Samples.size()-block.Offset);
        std::copy_n(block.Samples.data()+block.Offset,count,pcm.data()+copied);block.Offset+=count;copied+=count;
        if(block.Offset==block.Samples.size())PcmQueue.pop_front();
      }
      const auto id=Free.front();Free.pop_front();alBufferData(id,AL_FORMAT_STEREO16,pcm.data(),ALsizei(pcm.size()*2),48000);
      alSourceQueueBuffers(Source,1,&id);Queued.push_back({id,frames});State.PendingAudio-=frames;State.AudioQueued+=frames;
    }
    if(State.Started && !State.Paused && !Queued.empty()){
      ALint status=0;alGetSourcei(Source,AL_SOURCE_STATE,&status);if(status!=AL_PLAYING)alSourcePlay(Source);
    }
    CheckMovieAl();
  }
  void AudioFrame(const Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO>& item){
    const auto* f=item.Frame.raw();if(!f || f->nb_samples<0 || f->nb_samples>1000000)throw FormatError("invalid movie audio frame");
    if(!State.AudioDecoded && item.Timestamp.toDuration<std::chrono::microseconds>().count()!=0)
      throw FormatError("movie audio does not start at zero");
    const auto capacity=swr_get_out_samples(Convert,f->nb_samples);
    if(capacity<0 || capacity>2000000)throw FormatError("movie PCM conversion exceeds budget");
    std::vector<int16_t> samples(size_t(capacity)*2);uint8_t* target=reinterpret_cast<uint8_t*>(samples.data());
    const uint8_t* input[AV_NUM_DATA_POINTERS]{};
    const int planes=av_sample_fmt_is_planar(AVSampleFormat(f->format))?f->ch_layout.nb_channels:1;
    if(planes>AV_NUM_DATA_POINTERS)throw FormatError("movie audio plane count exceeds host");
    for(int i=0;i<planes;++i)input[i]=f->extended_data[i];
    const int converted=swr_convert(Convert,&target,capacity,input,f->nb_samples);
    if(converted<0)throw FormatError("movie PCM conversion failed");
    State.AudioDecoded+=f->nb_samples;
    const auto count=State.AudioLimit<0?uint64_t(converted):std::min(uint64_t(converted),uint64_t(std::max(int64_t(0),State.AudioLimit-int64_t(Converted))));
    Converted+=count;samples.resize(size_t(count)*2);
    if(count){State.PendingAudio+=count;PcmQueue.push_back({std::move(samples),0});}
  }
  void Pump(){
    for(size_t work=0;work<256;++work){
      bool progress=false;
      if(!State.VideoEof && Video.size()<90){
        VideoItem item;if(Decoder->VideoStream->FrameQueue.try_dequeue(item)){
          progress=true;if(item.Serial==INT32_MIN)State.VideoEof=true;
          else {
            if(!item.Timestamp.isValid())throw FormatError("movie frame has no presentation time");
            if(!State.VideoDecoded && item.Timestamp.toDuration<std::chrono::microseconds>().count()!=0)throw FormatError("movie video does not start at zero");
            Video.push_back(std::move(item));++State.VideoDecoded;
          }
        }
      }
      if(HasAudio && !State.AudioEof && State.PendingAudio<240000){
        Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> item;
        if(Decoder->AudioStream->FrameQueue.try_dequeue(item)){
          progress=true;if(item.Serial==INT32_MIN)State.AudioEof=true;else AudioFrame(item);
        }
      }
      if(!progress)break;
    }
    State.PendingVideo=Video.size();State.MaxPendingVideo=std::max(State.MaxPendingVideo,State.PendingVideo);
    State.MaxPendingAudio=std::max(State.MaxPendingAudio,State.PendingAudio);
  }
};
MoviePlayback::MoviePlayback():impl_(std::make_unique<Impl>()){}
MoviePlayback::~MoviePlayback()=default;
void MoviePlayback::Prepare(const std::filesystem::path& path){
  auto& s=*impl_;const auto volume=s.State.Volume;s.Close();s.State={};s.State.Volume=volume;s.PlayedBase=s.Converted=0;s.TailAt.reset();s.LastFrameEnd=0;
  s.Path=path;
  s.Decoder=std::make_unique<Video::FFmpegPlayer>();s.Decoder->InitDecodeOnly(true);Io::Stream* file=nullptr;
  if(Io::PhysicalFileStream::Create(path.string(),&file)!=IoError_OK)throw FormatError("cannot open movie");
  s.Decoder->Play(file,false,false);if(!s.Decoder->IsPlaying || !s.Decoder->VideoStream)throw FormatError("movie decoder unavailable");
  const auto rate=s.Decoder->VideoStream->AvStream.averageFrameRate();if(rate.getNumerator()<=0 || rate.getDenominator()<=0)throw FormatError("movie frame rate unavailable");
  s.FramePeriod=av_rescale(rate.getDenominator(),1000000,rate.getNumerator());s.HasAudio=bool(s.Decoder->AudioStream);
  s.State.AudioEof=!s.HasAudio;
  if(s.HasAudio){
    if(!alcGetCurrentContext())throw FormatError("movie requires the shared audio context");
    auto* codec=s.Decoder->AudioStream->CodecContext.raw();AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if(codec->sample_rate!=48000)throw FormatError("movie output currently requires authored48kHz audio");
    if(swr_alloc_set_opts2(&s.Convert,&stereo,AV_SAMPLE_FMT_S16,48000,&codec->ch_layout,codec->sample_fmt,codec->sample_rate,0,nullptr)<0 || swr_init(s.Convert)<0)
      throw FormatError("movie PCM output initialization failed");
    const auto* stream=s.Decoder->AudioStream->AvStream.raw();
    if(stream->duration!=AV_NOPTS_VALUE && stream->duration>0)s.State.AudioLimit=av_rescale_q(stream->duration,stream->time_base,AVRational{1,48000});
    alGenSources(1,&s.Source);alSourcef(s.Source,AL_GAIN,float(volume)/100);alSourcei(s.Source,AL_LOOPING,AL_FALSE);
    alGenBuffers(ALsizei(s.Buffers.size()),s.Buffers.data());for(auto id:s.Buffers)s.Free.push_back(id);CheckMovieAl();
  }
  s.State.Prepared=true;
}
bool MoviePlayback::Ready()const{const auto& s=*impl_;return s.State.Prepared && !s.State.Started && !s.Video.empty() && (!s.HasAudio || !s.Queued.empty());}
void MoviePlayback::Start(uint64_t now){
  if(!Ready())throw FormatError("movie start before decoded preroll");auto& s=*impl_;s.State.Started=true;s.StartedAt=now;s.Fill();
}
void MoviePlayback::SetVolume(int32_t percent){
  if(percent<0 || percent>100)throw FormatError("movie volume outside0..100");impl_->State.Volume=percent;
  if(impl_->Source){alSourcef(impl_->Source,AL_GAIN,float(percent)/100);CheckMovieAl();}
}
void MoviePlayback::SetPaused(bool paused,uint64_t now){
  auto& s=*impl_;
  if(!s.State.Prepared || !s.State.Started || s.State.Finished)
    throw FormatError("movie pause requires active playback");
  if(s.State.Paused==paused)return;
  if(paused){
    Update(now);
    // Update can finish a movie at the boundary; there is then no live source
    // to pause. Keep the actual EOF result rather than resurrecting playback.
    if(s.State.Finished)return;
    if(s.Source){alSourcePause(s.Source);CheckMovieAl();}
    s.PausedAt=now;s.State.Paused=true;
  }else {
    if(!s.PausedAt || now<*s.PausedAt)throw FormatError("movie resume precedes pause");
    const auto elapsed=now-*s.PausedAt;
    s.StartedAt+=elapsed;if(s.TailAt)*s.TailAt+=elapsed;
    s.State.Paused=false;s.PausedAt.reset();if(s.HasAudio)s.Fill();
  }
}
void MoviePlayback::Restart(){
  if(impl_->Path.empty())throw FormatError("movie restart has no source");
  const auto path=impl_->Path;Prepare(path);
}
void MoviePlayback::Update(uint64_t now){
  auto& s=*impl_;if(!s.State.Prepared || s.State.Finished)return;
  s.Reap();s.Pump();if(s.HasAudio)s.Fill();
  if(!s.State.Started || s.State.Paused)return;
  int64_t position=s.HasAudio?av_rescale(int64_t(s.State.AudioPlayed),1000000,48000):int64_t(now-s.StartedAt)*1000;
  if(s.HasAudio && s.State.AudioEof && !s.State.PendingAudio && s.Queued.empty()){
    if(!s.TailAt){s.TailAt=now;s.TailPosition=position;}position=s.TailPosition+int64_t(now-*s.TailAt)*1000;
  }
  s.State.PositionUs=position;
  while(!s.Video.empty()){
    const auto pts=s.Video.front().Timestamp.toDuration<std::chrono::microseconds>().count();if(pts>position)break;
    auto frame=std::move(s.Video.front());s.Video.pop_front();
    if(!Present)throw FormatError("movie has no frame presentation sink");Present(frame.Frame.raw(),pts);
    s.State.FramePtsUs=pts;s.LastFrameEnd=pts+s.FramePeriod;++s.State.VideoPresented;
  }
  s.State.PendingVideo=s.Video.size();
  if(s.State.VideoEof && s.State.AudioEof && s.Video.empty() && !s.State.PendingAudio && s.Queued.empty() && position>=s.LastFrameEnd){
    s.State.Finished=true;s.Decoder->Stop();
  }
}
void MoviePlayback::Stop(){impl_->Close();impl_->State.Finished=true;}
const MoviePlaybackState& MoviePlayback::State()const{return impl_->State;}
}
