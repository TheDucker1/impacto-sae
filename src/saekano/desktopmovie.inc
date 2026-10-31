// Reuse Impacto's FFmpegPlayer demux/decoder queues. This probe has no external
// ffmpeg process and supplies no replacement HCA/ADX/H.264 decoder. It measures
// source frames/PCM and lifecycle before the Saekano GL/audio host is attached.
static std::string MovieSourceDigest(const AVFrame* frame,int width,int height,int& size){
  if(!frame || frame->width!=width || frame->height!=height)throw FormatError("decoded movie resolution changed");
  const auto format=AVPixelFormat(frame->format);size=av_image_get_buffer_size(format,width,height,1);
  if(size<=0 || size>64*1024*1024)throw FormatError("invalid decoded movie video extent");
  std::vector<uint8_t> packed(size);
  if(av_image_copy_to_buffer(packed.data(),size,frame->data,frame->linesize,format,width,height,1)!=size)
    throw FormatError("cannot pack original decoded video planes");
  std::array<uint8_t,16> digest{};av_md5_sum(digest.data(),packed.data(),packed.size());
  constexpr char hex[]="0123456789abcdef";std::string hash;
  for(auto byte:digest){hash+=hex[byte>>4];hash+=hex[byte&15];}return hash;
}
int RunMoviePlaybackSession(const std::filesystem::path& root,const std::filesystem::path& output,uint32_t requestedFrames){
  bool created=false;
  try{
    if(requestedFrames>600)throw FormatError("movie session prefix exceeds10s; zero selects explicit EOF");
    const auto target=std::filesystem::weakly_canonical(output),source=std::filesystem::weakly_canonical(root);
    for(auto p=target;!p.empty();p=p.parent_path()){
      if(p==source || p==source.parent_path())throw FormatError("movie session overlaps original data");if(p==p.parent_path())break;
    }
    if(std::filesystem::exists(target))throw FormatError("choose a fresh movie session directory");std::filesystem::create_directories(target);created=true;
    ViewerWindow window(true);SdlCheck(SDL_SetWindowSize(window.Window,960,544),"movie session size");
    UiBlitRenderer background;PcMovieFrameLayer layer;ArchiveBgmResources music(root);MusicPlayback playback(music,true);
    MoviePlayback movie;std::ofstream frames(target/"frames.jsonl"),video(target/"video.jsonl"),pcm(target/"output.pcm",std::ios::binary);
    if(!frames || !video || !pcm)throw FormatError("cannot create movie output session files");
    size_t planeChecks=0;
    movie.Present=[&](const AVFrame* frame,int64_t pts){
      layer.Present(frame,pts);video<<"{\"pts_us\":"<<pts<<",\"upload\":"<<layer.Uploads;
      if(layer.Uploads<=2){
        int bytes=0;const auto hash=MovieSourceDigest(frame,frame->width,frame->height,bytes);
        video<<",\"source_md5\":\""<<hash<<"\"";
        const GLuint ids[]={layer.Planes.LumaId,layer.Planes.CbId,layer.Planes.CrId};GLint alignment=0;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glPixelStorei(GL_PACK_ALIGNMENT,1);
        for(int plane=0;plane<3;++plane){
          const int w=frame->width/(plane?2:1),h=frame->height/(plane?2:1);std::vector<uint8_t> pixels(size_t(w)*h);
          glBindTexture(GL_TEXTURE_2D,ids[plane]);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
          for(int row=0;row<h;++row)if(!std::equal(pixels.data()+size_t(row)*w,pixels.data()+size_t(row+1)*w,frame->data[plane]+size_t(row)*frame->linesize[plane]))
            throw FormatError("movie GPU plane differs from decoded visible rows");
          ++planeChecks;
        }
        glPixelStorei(GL_PACK_ALIGNMENT,alignment);GlCheck("movie plane storage verification");
      }
      video<<"}\n";
    };
    movie.Prepare(root/"OP1.usm");
    for(size_t attempt=0;!movie.Ready() && attempt<10000;++attempt){movie.Update(0);SDL_Delay(1);}
    if(!movie.Ready() || movie.State().AudioPlayed || movie.State().VideoPresented)throw FormatError("movie decoded preroll did not remain stopped/ready");
    movie.Start(0);uint64_t rendered=0;uint32_t frame=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    const auto draw=[&]{background.Draw({},960,544);if(movie.State().Started && !movie.State().Finished)layer.Draw();};
    for(;requestedFrames?frame<requestedFrames:!movie.State().Finished;++frame){
      if(frame>=7000 || std::chrono::steady_clock::now()>deadline)throw FormatError("movie device output session budget exhausted");
      if(requestedFrames>16){if(frame==16)movie.SetVolume(50);if(frame==32)movie.SetVolume(0);if(frame==48)movie.SetVolume(100);}
      movie.Update(uint64_t(frame)*16);draw();
      std::array<int16_t,1536> samples{};playback.Update(0.016f);playback.Render(samples);
      pcm.write(reinterpret_cast<const char*>(samples.data()),sizeof(samples));rendered+=768;
      const auto& s=movie.State();frames<<"{\"frame\":"<<frame<<",\"position_us\":"<<s.PositionUs<<",\"video_pts_us\":"<<s.FramePtsUs
        <<",\"video_presented\":"<<s.VideoPresented<<",\"audio_played\":"<<s.AudioPlayed<<",\"pending_audio\":"<<s.PendingAudio<<",\"pending_video\":"<<s.PendingVideo
        <<",\"volume\":"<<s.Volume<<",\"finished\":"<<s.Finished<<"}\n";
      if(frame==15 || frame==63)Capture(target/("frame-"+std::to_string(frame)+".bmp"),960,544);
      SdlCheck(SDL_GL_SwapWindow(window.Window),"movie session swap");
    }
    movie.Update(uint64_t(frame)*16);draw();if(!movie.State().Finished)Capture(target/"frame-final.bmp",960,544);
    const auto state=movie.State();if(planeChecks!=6 || !state.VideoPresented || !state.AudioPlayed)throw FormatError("movie session did not present actual video/audio");
    if(!requestedFrames && (!state.Finished || !state.AudioEof || !state.VideoEof || state.AudioPlayed!=uint64_t(state.AudioLimit)))
      throw FormatError("movie device EOF did not drain actual frame/audio queues");
    movie.Stop();layer.Retire();if(movie.State().Prepared || layer.Available)throw FormatError("movie output did not retire");
    frames.close();video.close();pcm.close();if(!frames || !video || !pcm)throw FormatError("cannot save movie output session");
    std::ofstream report(target/"session.json");report<<"{\"passed\":true,\"full_eof\":"<<(requestedFrames==0)<<",\"rendered_frames\":"<<rendered
      <<",\"audio_played\":"<<state.AudioPlayed<<",\"audio_limit\":"<<state.AudioLimit<<",\"video_presented\":"<<state.VideoPresented
      <<",\"position_us\":"<<state.PositionUs<<",\"gpu_plane_checks\":"<<planeChecks<<",\"max_pending_video\":"<<state.MaxPendingVideo
      <<",\"max_pending_audio\":"<<state.MaxPendingAudio<<",\"retired\":true,\"manual_acceptance\":false}\n";
    report.close();if(!report)throw FormatError("cannot save movie output summary");return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream f(output/"error.txt");f<<std::string(error.what()).substr(0,2048)<<'\n';}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano movie output session: %s",error.what());return 1;
  }
}
int RunMovieDecodeProbe(const std::filesystem::path& path,const std::filesystem::path& output,
    uint32_t requestedVideo,uint32_t requestedAudio){
  bool created=false;
  try{
    const bool full=requestedVideo==0 && requestedAudio==0;
    if(!full && (!requestedVideo || !requestedAudio || requestedVideo>120 || requestedAudio>240000))
      throw FormatError("movie decode prefix exceeds probe budget");
    const auto source=std::filesystem::weakly_canonical(path.parent_path()),target=std::filesystem::weakly_canonical(output);
    for(auto parent=target;!parent.empty();parent=parent.parent_path()){
      if(parent==source)throw FormatError("movie decode output overlaps source directory");
      if(parent==parent.parent_path())break;
    }
    if(std::filesystem::exists(target))throw FormatError("choose a fresh movie decode directory");
    std::filesystem::create_directories(target);created=true;
    Video::FFmpegPlayer player;player.InitDecodeOnly(true);Io::Stream* stream=nullptr;
    if(Io::PhysicalFileStream::Create(path.string(),&stream)!=IoError_OK)throw FormatError("cannot open movie source");
    player.Play(stream,false,false);
    if(!player.IsPlaying || !player.VideoStream || !player.AudioStream)
      throw FormatError("engine movie decoder did not open video and audio");
    const auto width=player.VideoStream->CodecContext.width(),height=player.VideoStream->CodecContext.height();
    const auto rate=player.AudioStream->CodecContext.sampleRate();
    if(width<=0 || height<=0 || width>4096 || height>4096 || rate!=48000)
      throw FormatError("unexpected movie probe dimensions/sample rate");
    std::ofstream video(target/"video.jsonl"),audio(target/"audio.pcm",std::ios::binary);
    if(!video || !audio)throw FormatError("cannot create movie decode reports");
    uint64_t videoCount=0,audioCount=0,writtenAudio=0;bool videoEof=false,audioEof=false;
    int64_t previousPts=INT64_MIN,firstAudioPts=INT64_MIN;std::string firstVideoHash;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(full?!(videoEof && audioEof):(videoCount<requestedVideo || writtenAudio<requestedAudio)){
      if(std::chrono::steady_clock::now()>deadline)throw FormatError("engine movie decode queues stalled");
      bool work=false;Video::AVDecodedItem<AVMEDIA_TYPE_VIDEO> v;
      if(!videoEof && player.VideoStream->FrameQueue.try_dequeue(v)){
        work=true;
        if(v.Serial==INT32_MIN)videoEof=true;
        else {
          const auto* f=v.Frame.raw();const auto pts=v.Timestamp.toDuration<std::chrono::microseconds>().count();
          if(!f || pts<previousPts || v.Timestamp.timestamp()==AV_NOPTS_VALUE)
            throw FormatError("invalid movie timestamp: frame="+std::to_string(videoCount)+" stamp="+std::to_string(v.Timestamp.timestamp())+
              " raw="+std::to_string(f?f->pts:0)+" best="+std::to_string(f?f->best_effort_timestamp:0)+" tb="+
              std::to_string(v.Timestamp.timebase().getNumerator())+"/"+std::to_string(v.Timestamp.timebase().getDenominator()));
          previousPts=pts;
          if(videoCount<(full?24:requestedVideo)){
            int size=0;const auto hash=MovieSourceDigest(f,width,height,size);if(!videoCount)firstVideoHash=hash;
            video<<"{\"frame\":"<<videoCount<<",\"pts_us\":"<<pts<<",\"bytes\":"<<size<<",\"md5\":\""<<hash<<"\"}\n";
          }
          ++videoCount;
        }
      }
      Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> a;
      if(!audioEof && player.AudioStream->FrameQueue.try_dequeue(a)){
        work=true;
        if(a.Serial==INT32_MIN)audioEof=true;
        else {
          const auto* f=a.Frame.raw();if(!f || f->ch_layout.nb_channels!=2 || f->nb_samples<0)
            throw FormatError("unexpected decoded movie audio layout");
          if(firstAudioPts==INT64_MIN)firstAudioPts=a.Timestamp.toDuration<std::chrono::microseconds>().count();
          const size_t take=full?size_t(f->nb_samples):std::min(size_t(f->nb_samples),size_t(requestedAudio-writtenAudio));
          std::vector<int16_t> samples(take*2);
          // PCM layout packing only. ADX decoding is the existing player's codec.
          if(f->format==AV_SAMPLE_FMT_S16P){
            const auto* left=reinterpret_cast<const int16_t*>(f->extended_data[0]);
            const auto* right=reinterpret_cast<const int16_t*>(f->extended_data[1]);
            for(size_t i=0;i<take;++i){samples[i*2]=left[i];samples[i*2+1]=right[i];}
          }else if(f->format==AV_SAMPLE_FMT_S16)std::memcpy(samples.data(),f->extended_data[0],samples.size()*2);
          else throw FormatError("movie probe requires decoded S16 PCM");
          audio.write(reinterpret_cast<const char*>(samples.data()),samples.size()*2);writtenAudio+=take;audioCount+=f->nb_samples;
        }
      }
      if((videoEof && videoCount<requestedVideo) || (audioEof && writtenAudio<requestedAudio))
        throw FormatError("movie ended before requested decode prefix");
      if(!work)SDL_Delay(1);
    }
    player.Stop();
    if(player.IsPlaying || player.ReadThread.joinable() || player.VideoStream || player.AudioStream)
      throw FormatError("engine movie decoder retained workers after stop");
    // Reuse this same player, not a new process/object or a cached prior frame.
    stream=nullptr;
    if(Io::PhysicalFileStream::Create(path.string(),&stream)!=IoError_OK)throw FormatError("cannot reopen movie source");
    player.Play(stream,false,false);if(!player.IsPlaying || !player.VideoStream)throw FormatError("movie decoder did not restart");
    Video::AVDecodedItem<AVMEDIA_TYPE_VIDEO> rebound;bool reboundReady=false;
    const auto restartDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<restartDeadline){
      // Both bounded queues need consumers while H.264 prerolls. This probe
      // discards restart audio; a playback host must retain/queue that PCM.
      Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> preroll;
      while(player.AudioStream && player.AudioStream->FrameQueue.try_dequeue(preroll)){}
      if(player.VideoStream->FrameQueue.try_dequeue(rebound)){reboundReady=true;break;}SDL_Delay(1);
    }
    int reboundSize=0;
    const auto reboundHash=reboundReady && rebound.Serial!=INT32_MIN?MovieSourceDigest(rebound.Frame.raw(),width,height,reboundSize):std::string{};
    if(!reboundReady || rebound.Serial==INT32_MIN || rebound.Timestamp.timestamp()!=0 || reboundHash!=firstVideoHash)
      throw FormatError("movie restart mismatch: ready="+std::to_string(reboundReady)+" serial="+std::to_string(rebound.Serial)+
        " pts="+std::to_string(rebound.Timestamp.timestamp())+" hash="+reboundHash+" expected="+firstVideoHash);
    player.Stop();
    if(player.IsPlaying || player.ReadThread.joinable() || player.VideoStream || player.AudioStream)
      throw FormatError("restarted movie decoder retained workers");
    video.close();audio.close();if(!video || !audio)throw FormatError("cannot save movie decode reports");
    std::ofstream report(target/"decode.json");
    report<<"{\"passed\":true,\"existing_impacto_player\":true,\"decode_only\":true,\"missing_pts_from_stream_metadata\":true,\"full_eof\":"<<full
      <<",\"video_eof\":"<<videoEof<<",\"audio_eof\":"<<audioEof<<",\"video_frames\":"<<videoCount<<",\"audio_frames\":"<<audioCount
      <<",\"written_audio_frames\":"<<writtenAudio<<",\"first_audio_pts_us\":"<<firstAudioPts<<",\"width\":"<<width<<",\"height\":"<<height
      <<",\"workers_retired\":true,\"same_player_restart\":true,\"external_ffmpeg_used\":false,\"playback_bound\":false}\n";
    report.close();if(!report)throw FormatError("cannot save movie decoder summary");return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream report(output/"error.txt");report<<std::string(error.what()).substr(0,2048)<<'\n';}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano existing movie decoder: %s",error.what());return 1;
  }
}
