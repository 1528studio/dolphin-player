// Android Media Player - Based on FFPlay, KMP2, and dranger.com tutorials
//  
// Description:
//   - Support for playing audio and video files
//   - Support for subtitles from various subtitle files
//   - Support for pausing and playing of audio & video files
//   - Support for forward and rewind of audio & video files
//   - Support for player on screen controls

// ChangeLog
// Author: AA
// Skip Frames Logic: Nareshprasad

//List of features to be enabled or disabled
// Enable the list of features required for this release
// by removing or adding the // before #define

//Convert YUV to RGB for Source width & height, 
// Converting it to destination
// Width and height will result in performance hit
// Performance Hit is observed when destination w, h is 
// modified from source
#define BROOV_PLAYER_SOURCE_WIDTH_HEIGHT

#define BROOV_VIDEO_SKIP

#define BROOV_VIDEO_THREAD
//#define BROOV_FFMPEG_OLD
//#define BROOV_WITHOUT_AUDIO
//#define BROOV_ONLY_AUDIO
//#define BROOV_PLAYER_PROFILING 
//#define BROOV_PLAYER_DEBUG
//#define BROOV_PLAYER_DISPLAY_FRAMES_IMMEDIATELY
//#define TEST_SKIP_LOGIC
//#define BROOV_FRAME_SKIP_DEBUG
//#define BROOV_PLAYER_DISPLAY_FPS

//#define BROOV_FRAME_RATE
//#define BROOV_X86_RELEASE

#include "broov_player.h"
#include "subreader.h"
#include "broov_font.h"

#ifdef BROOV_X86_RELEASE
#else
#include "yuv2rgb.h"
#endif

extern JavaVM *gBroovJniVM;

const char program_name[]     = "Dolphin Player";
const int  program_birth_year = 2011;

// Variables used from Upper Layer
// Begin
#define VIDEO_OUTPUT_RGB565  0
#define VIDEO_OUTPUT_RGB8888 1

//static PixelFormat dst_pix_fmt = PIX_FMT_RGB565;
static PixelFormat dst_pix_fmt = PIX_FMT_RGBA;
//static PixelFormat dst_pix_fmt = PIX_FMT_ABGR;
//static PixelFormat dst_pix_fmt = PIX_FMT_BGRA; //somewhat ok
//static PixelFormat dst_pix_fmt = PIX_FMT_ABGR;
//static PixelFormat dst_pix_fmt = PIX_FMT_ARGB;

int            BROOV_VIDEO_MIN_BUFFER_SIZE=256000;
int            BROOV_VIDEO_MAX_BUFFER_SIZE=2048576;
int            BROOV_TOTAL_MAX_BUFFER_SIZE=3048576;
int            MAX_AUDIOQ_SIZE=524288;

// 0 - RGB565, 1-rgb8888
int            g_alternate_skip_change    = 0;
int            g_last_skip_type           = AVDISCARD_DEFAULT;
int            g_video_output_rgb_type    = VIDEO_OUTPUT_RGB8888; 
int            g_video_yuv_rgb_asm        = 1;
int            g_subtitle_encoding_type   = 0;
static int     g_skip_frames              = 0;
static int     g_aspect_ratio_type        = 0;

static int     g_total_duration           = 0;
static int     g_current_duration         = 0;
static int     g_seek_duration            = 0;
static int     g_seek_success             = 0;

static int     g_eof                      = 0;
static int     g_show_subtitle            = 1;
static int     g_autoexit                 = 0;
static int     g_loop                     = 0;
static int     g_debug                    = 0;
static int     g_audio_file_type          = 0;
static int     g_asm_yuv2rgb              = 1;

static int64_t start_time                 = AV_NOPTS_VALUE;
static int64_t duration                   = AV_NOPTS_VALUE;

// End 

#ifdef BROOV_VIDEO_THREAD
static bool in_sync = true;
#endif

static SDL_Surface     *screen;

static int dither;

AVFrame *g_video_frame;

#ifdef BROOV_FRAME_RATE

#define BROOV_DEFAULT_NUM_FRAMES 7

#define MAX_FPS_FRAMES 25
#define MAX_FPS_SETS   3

#define BROOV_FRAME_DECODE_RATE     0
#define BROOV_FRAME_YUV_2_RGB_RATE  1
#define BROOV_FRAME_DISPLAY_RATE    2

typedef float FrameRateType;
void init_frame_rate(FrameRateType value, int n, int type);
FrameRateType get_average_of_N(FrameRateType new_num, int type);
int get_frames_to_skip();

#endif

#ifdef BROOV_VIDEO_SKIP
//static variable to store the framespersecond value from calculate__frames_per_second
static double player_fps_temp;
static double player_fps_tempin;
static double file_fps;
static bool   skipped = false;
static double avg_outputtime_missed = 0.0;
static double video_lag_err=0.0;

static int skipafternumberofframes=5;
static int skipafternumberofframessmall=4;
static bool alternateskip = true;
static int skipnumberoftimes1 = 2;
static int skipnumberoftimes2 = 3;
static int skipnumberoftimeslarge1 = 5;
static int skipnumberoftimeslarge2 = 7;	

static double videoTime = 0;
static double vlref_clock = 0.0;

static char   firstVideoPTSSet = false;
static double firstVideoPTS = 0.0;
static double lastVideoPTS = 0.0;

static double secondsPerFrame = 0; //   1/framerate
static int    skipmode = 0;
static char   seenKeyFrameAfterCaughtUp = false;

static double calculate_file_fps(VideoState *is);
void initialize_video_skip_variables();
static void calculate_avg_output_time(double start_output_picture2, double end_output_picture2);
static void calculate_frames_per_second_internal();
static int skip_now(VideoState *is);
#endif /*  #ifdef BROOV_VIDEO_SKIP */

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
static VideoState *global_video_state;
static VideoState *cur_stream;
static int64_t     audio_callback_time;

AVPacket           flush_pkt;

//SWS_BICUBIC, SWS_BILINEAR, SWS_FAST_BILINEAR
static int         sws_flags = SWS_BICUBIC;

static int         rdftspeed=20;
static int         framedrop=0;

static int debug_mv = 0;
static int step = 0;
static int thread_count = 1;
static int workaround_bugs = 1;
static int fast = 0;
static int genpts = 0;
static int lowres = 0;
static int idct = FF_IDCT_AUTO;

static enum AVDiscard skip_frame= AVDISCARD_DEFAULT;
static enum AVDiscard skip_idct= AVDISCARD_DEFAULT;
static enum AVDiscard skip_loop_filter= AVDISCARD_DEFAULT;

//static enum AVDiscard skip_frame= AVDISCARD_BIDIR;
//static enum AVDiscard skip_idct= AVDISCARD_BIDIR;
//static enum AVDiscard skip_loop_filter= AVDISCARD_BIDIR;

#ifdef BROOV_FFMPEG_OLD
static int error_recognition = FF_ER_CAREFUL;
#endif
static int error_concealment = 3;

static int seek_by_bytes=-1;

static int decoder_reorder_pts= -1;

static int av_sync_type = DEFAULT_AV_SYNC_TYPE;

static const char *input_filename;
static const char *window_title;
int fs_screen_width  = 640;
int fs_screen_height = 480;

int g_aspect_ratio_x, g_aspect_ratio_y;
int g_aspect_ratio_w, g_aspect_ratio_h;

static int fps_display_x = 500;

static int screen_width = 0;
static int screen_height = 0;
static int frame_width = 0;
static int frame_height = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_NONE;
static int audio_disable;
static int video_disable;

double player_fps;
double player_fpsin;

static int video_pkt_dts=0;
static int audio_pkt_dts=0;

uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;
static double audio_clock_delta = 0;

static void do_exit(void);

/* get the current audio output buffer size, in samples. With SDL, we
   cannot have a precise information */
static int audio_write_get_buf_size(VideoState *is)
{
	return is->audio_buf_size - is->audio_buf_index;
}

static double get_audio_clock(VideoState *is) 
{
	double pts = 0;
	int hw_buf_size, bytes_per_sec;

	pts = is->audio_clock + audio_clock_delta; /* maintained in the audio thread */
	hw_buf_size = audio_write_get_buf_size(is);
	bytes_per_sec = 0;

	if (is->audio_st) {
		bytes_per_sec = is->audio_st->codec->sample_rate * 2 * is->audio_st->codec->channels;
	}

	if (bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}

	return pts;
}

/* get the current video clock value */
static double get_video_clock(VideoState *is)
{
	if (is->paused) {
		return is->video_current_pts;
	} else {
		return is->video_current_pts_drift + av_gettime() / 1000000.0;
	}
}

static double get_external_clock(VideoState *is) 
{
	return av_gettime() / 1000000.0;

}

static double get_master_clock(VideoState *is) 
{
	double val;

	if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		if (is->video_st)
			val = get_video_clock(is);
		else
			val = get_audio_clock(is);
	} else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		if (is->audio_st)
			val = get_audio_clock(is);
		else
			val = get_video_clock(is);
	} else {
		val = get_external_clock(is);
	}
	return val;

}

void fill_rectangle(SDL_Surface *screen,
		int x, int y, int w, int h, int color)
{
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	SDL_FillRect(screen, &rect, color);
}

void clear_screen(SDL_Surface *screen)
{
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Inside clear_screen");

	SDL_Rect rect = {0, 0, fs_screen_width, fs_screen_height};
	SDL_SetRenderDrawColor(0, 0, 0, 0);
	SDL_RenderFillRect(&rect);
	SDL_RenderPresent();

}

static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes) 
{
	if(!is->seek_req) {
		is->seek_pos = pos;
		is->seek_rel = rel;
		is->seek_flags &= ~AVSEEK_FLAG_BYTE;

		if (seek_by_bytes)
			is->seek_flags |= AVSEEK_FLAG_BYTE;
		is->seek_req_special = 0;
		is->seek_req = 1;
	}
} /* stream_seek() */

static void stream_seek_special(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes) 
{
	if(!is->seek_req) {
		is->seek_pos = pos;
		is->seek_rel = rel;
		is->seek_flags &= ~AVSEEK_FLAG_BYTE;

		if (seek_by_bytes)
			is->seek_flags |= AVSEEK_FLAG_BYTE;
		is->seek_req_special = 1;
		is->seek_req = 1;
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek Requested:%d", g_seek_duration);
	}
} /* stream_seek() */


/* pause or resume the video */
static void stream_pause(VideoState *is)
{
	if (is->paused) {
		is->frame_timer += av_gettime() / 1000000.0 + is->video_current_pts_drift - is->video_current_pts;
		if(is->read_pause_return != AVERROR(ENOSYS)){
			is->video_current_pts = is->video_current_pts_drift + av_gettime() / 1000000.0;
		}
		is->video_current_pts_drift = is->video_current_pts - av_gettime() / 1000000.0;
	}
	is->paused = !is->paused;
}

static double compute_target_time(double frame_current_pts, VideoState *is)
{
	double delay, sync_threshold, diff;

	/* compute nominal delay */
	delay = frame_current_pts - is->frame_last_pts;
	if (delay <= 0 || delay >= 10.0) {
		/* if incorrect delay, use previous one */
		delay = is->frame_last_delay;
	} else {
		is->frame_last_delay = delay;
	}
	is->frame_last_pts = frame_current_pts;

	is->frame_timer += delay;

#ifdef BROOV_DEBUG_PLAYER
	{
		char log_msg[128];
		sprintf(log_msg, "video: delay=%0.3f actual_delay=%0.3f pts=%0.3f A-V=%f\n", delay, actual_delay, frame_current_pts, -diff);
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", log_msg);
	}
#endif

	return is->frame_timer;
}


/* return the new audio buffer size (samples can be added or deleted
   to get better sync if video or external master clock) */
static int synchronize_audio(VideoState *is, short *samples,
		int samples_size1, double pts) 
{
	int n;
	int samples_size;
	double ref_clock;

	n = 2 * is->audio_st->codec->channels;
	samples_size = samples_size1;

	/* if not master, then we try to remove or add samples to correct the clock */
	if (((is->av_sync_type == AV_SYNC_VIDEO_MASTER) && 
				(is->video_st)) ||
			(is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)) {
		double diff, avg_diff;
		int wanted_size, min_size, max_size, nb_samples;

		ref_clock = get_master_clock(is);
		diff = get_audio_clock(is) - ref_clock;

		if(diff < AV_NOSYNC_THRESHOLD) {
			// accumulate the diffs
			is->audio_diff_cum = diff + is->audio_diff_avg_coef
				* is->audio_diff_cum;
			if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
				/* not enough measures to have a correct estimate */
				is->audio_diff_avg_count++;
			} else {
				/* estimate the A-V difference */
				avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

				if (fabs(avg_diff) >= is->audio_diff_threshold) {
					wanted_size = samples_size + ((int)(diff * is->audio_st->codec->sample_rate) * n);
					nb_samples = samples_size / n;

					min_size = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
					max_size = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
					if(wanted_size < min_size) {
						wanted_size = min_size;
					} else if (wanted_size > max_size) {
						wanted_size = max_size;
					}

					/* add or remove samples to correction the synchro */

					if (wanted_size < samples_size) {
						/* remove samples */
						samples_size = wanted_size;
					} else if(wanted_size > samples_size) {
						uint8_t *samples_end, *q;
						int nb;

						/* add samples by copying final sample*/
						nb = (samples_size - wanted_size);
						samples_end = (uint8_t *)samples + samples_size - n;
						q = samples_end + n;
						while(nb > 0) {
							memcpy(q, samples_end, n);
							q += n;
							nb -= n;
						}

						samples_size = wanted_size;
					}
				}
			}
		} else {

			/* too big difference : may be initial PTS errors, so
			   reset A-V filter */
			is->audio_diff_avg_count = 0;
			is->audio_diff_cum = 0;
		}
	}

	return samples_size;
}

/* decode one audio frame and returns its uncompressed size */
static int audio_decode_frame(VideoState *is, double *pts_ptr) 
{
	AVPacket *pkt_temp = &is->audio_pkt_temp;
	AVPacket *pkt = &is->audio_pkt;
	AVCodecContext *dec= is->audio_st->codec;
	int n, len1, data_size;
	double pts;

	for(;;) {
		/* NOTE: the audio packet can contain several frames */
		while (pkt_temp->size > 0) {
			data_size = sizeof(is->audio_buf1);
			len1 = avcodec_decode_audio3(dec,
					(int16_t *)is->audio_buf1, &data_size,
					pkt_temp);
			if (len1 < 0) {
				/* if error, we skip the frame */
				pkt_temp->size = 0;
				break;
			}

			pkt_temp->data += len1;
			pkt_temp->size -= len1;
			if (data_size <= 0)
				continue;

			is->audio_buf= is->audio_buf1;

			/* if no pts, then compute it */
			pts = is->audio_clock;
			*pts_ptr = pts;
			n = 2 * dec->channels;
			is->audio_clock += (double)data_size /
				(double)(n * dec->sample_rate);

#ifdef DEBUG_PLAYER
			{
				static double last_clock;
				printf("audio: delay=%0.3f clock=%0.3f pts=%0.3f\n",
						is->audio_clock - last_clock,
						is->audio_clock, pts);
				last_clock = is->audio_clock;
			}
#endif
			return data_size;
		}

		/* free the current packet */
		if (pkt->data)
			av_free_packet(pkt);

		if (is->paused || is->audioq.abort_request) {
			return -1;
		}

		/* read next packet */
		if (packet_queue_get(&is->audioq, pkt, 1) < 0)
			return -1;
		if(pkt->data == flush_pkt.data){
			avcodec_flush_buffers(dec);
			continue;
		}

		pkt_temp->data = pkt->data;
		pkt_temp->size = pkt->size;

		/* if update the audio clock with the pts */
		if (pkt->pts != AV_NOPTS_VALUE) {
			is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
		}
	}
}

static inline int compute_mod(int a, int b)
{
	a = a % b;
	if (a >= 0)
		return a;
	else
		return a + b;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) 
{

	VideoState *is = (VideoState *)userdata;
	int len1, audio_size;
	double pts;

	audio_callback_time = av_gettime();

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "audio_callback::len::%d", len);

	while (len > 0) {

		if (is->audio_buf_index >= is->audio_buf_size) {
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(is, &pts);

#ifdef BROOV_VIDEO_THREAD
			while (audio_size < 0) {
				if (is->abort_request) break;
				SDL_Delay(10);  
				audio_size = audio_decode_frame(is, &pts);
			}

#else
			while (audio_size < 0) {
				if (is->abort_request) break;
				SDL_Delay(10);  
				audio_size = audio_decode_frame(is, &pts);
			}
#endif

			if (audio_size < 0) {
				/* If error, output silence */
				is->audio_buf = is->audio_buf1;
				is->audio_buf_size = 1024;
#ifdef BROOV_VIDEO_THREAD
				//memset(is->audio_buf, 0x80, is->audio_buf_size);
				memset(is->audio_buf, 0, is->audio_buf_size);
#else
				memset(is->audio_buf, 0, is->audio_buf_size);
#endif
			} else {

				audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,
						audio_size, pts);
				is->audio_buf_size = audio_size;
			}
			is->audio_buf_index = 0;
		}
		len1 = is->audio_buf_size - is->audio_buf_index;
		if(len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}
}

/* return current time (in seconds) */
double current_time(void)
{
	struct timeval tv;
#ifdef __VMS
	(void) gettimeofday(&tv, NULL );
#else
	struct timezone tz;
	(void) gettimeofday(&tv, &tz);
#endif
	return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void calculate_frames_per_second()
{
	static double st=0;
	static double ct=0;
	static double seconds=0;
	static int frames=0;

	if (st == 0) {
		st = ct = seconds = current_time();
	}

	{
		frames++;
		if (frames>25) {
			double tt = current_time();

			double dt; //= tt - ct; //elapsed time

			ct = tt; //current time

			dt = ct - seconds;
			if (dt >= 5.0)
			{
				player_fps = frames/dt;
#ifdef BROOV_VIDEO_SKIP
				player_fps_temp = player_fps;
#endif
				{
					char log_msg[16];
					sprintf(log_msg, "%2.1f FPS", player_fps);
					__android_log_print(ANDROID_LOG_INFO, "AV FPS", log_msg);
					//fps_draw_msg(screen, fps_display_x, sy, log_msg);
				}

				//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%d frames in %3.1f seconds = %6.3f FPS", frames, dt, player_fps);
				seconds = ct;
				frames = 0;
			}
		}

	}

}

void calculate_duration(AVFormatContext *ic)
{
	if (ic && (ic->duration != AV_NOPTS_VALUE)) 
	{
		int secs;
		secs = ic->duration / AV_TIME_BASE;
		//us = ic->duration % AV_TIME_BASE;

		g_total_duration=secs;
	} 
}

static void broov_set_current_duration(ULONG secs)
{
	static int reset_counter=0;

	//If the current duration is > 99 hrs then error in computation somewhere
	if (secs >= 356400) {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Unexpected CurrentDuration:%ld ", secs);
		g_current_duration = 0;
		return;
	}

	if (g_seek_success) {
		if ((secs == g_current_duration) || 
				(secs == g_current_duration-1) ||
				(secs == g_current_duration-2) ||
				(secs == g_current_duration+1) ||
				(secs == g_current_duration+2) ) {
			g_seek_success = 0;
			g_current_duration = secs;
			reset_counter=0;
		} else {

			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Skipping Set Current Duration:%ld ", secs);
			//Fail safe mechanism
			reset_counter++; 
			if (reset_counter >=5) {
				g_seek_success = 0;
				reset_counter=0;
			}
		}

	} else {
		g_current_duration = secs;
	}

}

static int try_to_set_best_video_mode(int w, int h, int rgb565)
{
	const SDL_VideoInfo *vi = SDL_GetVideoInfo();
	if (vi) 
	{
		fs_screen_width  = vi->current_w;
		fs_screen_height = vi->current_h;
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "FullScreen Width:%d Height:%d", fs_screen_width, fs_screen_height);

		fps_display_x = (int) ((double)560.0 * fs_screen_width/640.0);
		broov_gui_update_positions();
		w = fs_screen_width;
		h = fs_screen_height;
		g_aspect_ratio_x = 0;
		g_aspect_ratio_y = 0;
		g_aspect_ratio_w = fs_screen_width;
		g_aspect_ratio_h = fs_screen_height;
	}

	//int flags = SDL_SWSURFACE;
	//int flags = SDL_SWSURFACE|SDL_FULLSCREEN;

	//int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL | 
	//                SDL_FULLSCREEN;

	int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL | 
		SDL_RESIZABLE | SDL_FULLSCREEN;
	int bpp;

        if (rgb565) { bpp = 16; } else { bpp=24; }
	screen = SDL_SetVideoMode(w, h, bpp, flags);

	if (!screen) {
		bpp=32;
		screen = SDL_SetVideoMode(w, h, bpp, flags);

		if (!screen) {
                        if (rgb565) { bpp = 16; } else { bpp=24; }
			flags = SDL_SWSURFACE;
			screen = SDL_SetVideoMode(w, h, bpp, flags);
			if (!screen) {
				bpp = 32;
				screen = SDL_SetVideoMode(w, h, bpp, flags);

			}
		}
	}

	if (screen) {
		return 1;
	}

	return 0;
} /* End of try_to_set_best_video_mode method */

static void ConvertDataToReverse(uint8_t *buffer, int w, int h)
{
	int i;
	int len;
	uint8_t swap;

	len = (w*4) * h;

	for (i=0; i<len; i+=4) {
		// 0 1 2 3 to  3 2 1 0

		// Swap offset 0 & 3
		swap = buffer[i]; // offset 0
		buffer[i] = buffer[i+3];
		buffer[i+3] = swap;

		// Swap offset 1 & 2
		swap = buffer[i+1]; // offset 1
		buffer[i+1] = buffer[i+2];
		buffer[i+2] = swap;

	}

}

static void ConvertDataFromRGBAToARGB(uint8_t *buffer, int w, int h)
{
	int i;
	int len;
	uint8_t swap0;
	uint8_t swap1;
	uint8_t swap2;
	uint8_t swap3;

	len = (w*4) * h;

	for (i=0; i<len; i+=4) {
		// 0 1 2 3 to  3 0 1 2
                swap0 = buffer[i];
                swap1 = buffer[i+1];
                swap2 = buffer[i+2];
                swap3 = buffer[i+3];

                buffer[i]   = swap3;
                buffer[i+1] = swap0;
                buffer[i+2] = swap1;
                buffer[i+3] = swap2;
	}

}

static void rgb_video_image_display(VideoState *is)
{
	ULONG ulRefClock;
	SDL_Rect rect;
	VideoPicture *vp;

#ifdef BROOV_FRAME_RATE
	static double st=0;
	static double ct=0;

	st = current_time();
#endif
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Inside rgb video image display");

	vp = &is->pictq[is->pictq_rindex];

	if (!vp->pFrameRGB) return;

	if ((vp->dst_width != g_aspect_ratio_w) ||
	    (vp->dst_height != g_aspect_ratio_h)) {
		//aspect ratio got changed
		//do not print until the correct aspect ratio frame
		//received
		return;
	}

	rect.x = vp->x;
	rect.y = vp->y;
	rect.w = vp->dst_width;
	rect.h = vp->dst_height;

	if (is->use_sub)
	{
		ulRefClock = get_master_clock(is) * 100;
		subClearDisplay(screen);
		if (!(subInTime(ulRefClock)))
		{
			subFindNext(ulRefClock);
		}
	}

        //if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {
        //     if (g_asm_yuv2rgb == 1) {
        //         //ConvertDataToReverse(vp->buffer, vp->width, vp->height);
        //         ConvertDataFromRGBAToARGB(vp->buffer, vp->width, vp->height);
        //     }
        //}

	if (!vp->display_rgb_text) {
                if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
			vp->display_rgb_text = SDL_CreateTexture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, vp->width, vp->height);
#else
			vp->display_rgb_text = SDL_CreateTexture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, vp->dst_width, vp->dst_height);
#endif

		} else {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
			vp->display_rgb_text = SDL_CreateTexture(SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, vp->width, vp->height);
#else
			vp->display_rgb_text = SDL_CreateTexture(SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, vp->dst_width, vp->dst_height);
#endif
		}
		SDL_UpdateTexture(vp->display_rgb_text, NULL, vp->pFrameRGB->data[0], vp->pFrameRGB->linesize[0]);

	} else {
		SDL_UpdateTexture(vp->display_rgb_text, NULL, vp->pFrameRGB->data[0], vp->pFrameRGB->linesize[0]);

	}

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
	//SDL_RenderCopy(vp->display_rgb_text, NULL, NULL);
	SDL_RenderCopy(vp->display_rgb_text, NULL, &rect);
#else
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "B");
	SDL_RenderCopy(vp->display_rgb_text, NULL, &rect);
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "A");
#endif

	if (is->use_sub)
	{
		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Using subTitles");
		if (subInTime(ulRefClock)) {
			subDisplay(screen, 1);
		} else {
			subFindNext(ulRefClock);
		}

	}

	{
		ULONG dur = (ULONG)get_master_clock(is);
		// update the value, only if a value change happens
		if (dur != g_current_duration) {
			broov_set_current_duration(dur);
		}
	}

#ifdef BROOV_PLAYER_DISPLAY_FPS
	// Do not change this position, as we copy the
	// FPS to be print on screen
	calculate_frames_per_second();
#endif

	SDL_RenderPresent();

#ifdef BROOV_FRAME_RATE
	ct = current_time();
	get_average_of_N( (ct-st), BROOV_FRAME_DISPLAY_RATE);
#endif

}

static void video_refresh_rgb_timer(void *userdata) 
{
	VideoState *is = (VideoState *)userdata;


	if (is->video_st) {

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VRT");

		if (is->pictq_size == 0) {
			//nothing to do, no picture to display in the queue

		} else {
			VideoPicture *vp;

			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "DSP");
			double time= av_gettime()/1000000.0;

			/* dequeue the picture */
			vp = &is->pictq[is->pictq_rindex];

#ifdef BROOV_PLAYER_DISPLAY_FRAMES_IMMEDIATELY
#else
			//Frame delay is already computed and timer accordingly started
			if (time < vp->target_clock) {
				return;
			}
#endif

			/* update current video pts */
			is->video_current_pts = vp->pts;
			is->video_current_pts_drift = is->video_current_pts - time;
			is->video_current_pos = vp->pos;
			is->video_current_pts_time = av_gettime();

			/* show the picture! */
			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Before Video Display");
			rgb_video_image_display(is);
			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "After Video Display");

			/* update queue for next picture! */
			if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}

			SDL_LockMutex(is->pictq_mutex);
			is->pictq_size--;
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);

		}
	}
	else if (is->audio_st) {
		if (!screen) {
                        if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {
			    try_to_set_best_video_mode(fs_screen_width, fs_screen_height, 0);
                        } else {
			    try_to_set_best_video_mode(fs_screen_width, fs_screen_height, 1);
                        }
		}

		//if (screen && g_audio_file_type) {
		//	broov_set_current_duration((ULONG) get_master_clock(is));
		//}
		{
			ULONG dur = (ULONG)get_master_clock(is);
			// update the value, only if a value change happens
			if (dur != g_current_duration) {
				broov_set_current_duration(dur);
			}
		}

	} else {
		// none is matched

	}

#ifdef BROOV_DEBUG_PLAYER
	print_status(is);
#endif	

}

static void alloc_rgb_picture(void *userdata) 
{
	int  width, height; /* destination width & height */
	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;

	vp = &is->pictq[is->pictq_windex];
	if (vp->pFrameRGB) {
		// we already have one make another, bigger/smaller
		if (vp->pFrameRGB) {
			av_free(vp->pFrameRGB); vp->pFrameRGB=0;
		}
		if (vp->buffer) {
			av_free(vp->buffer); vp->buffer=0;
		}
		if (vp->display_rgb_text) {
			SDL_DestroyTexture(vp->display_rgb_text);
			vp->display_rgb_text=0;
		}
	}

	/* Source video file, width & height */
	vp->width = is->video_st->codec->width;
	vp->height = is->video_st->codec->height;

	width = g_aspect_ratio_w;
	height = g_aspect_ratio_h;

	vp->x = g_aspect_ratio_x;
	vp->y = g_aspect_ratio_y;
	vp->dst_width  = g_aspect_ratio_w;
	vp->dst_height = g_aspect_ratio_h;

	vp->pix_fmt = is->video_st->codec->pix_fmt;

	// Allocate a place to put our YUV image on that screen
	vp->pFrameRGB = avcodec_alloc_frame();
        if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
	vp->numBytes = avpicture_get_size(PIX_FMT_RGBA, vp->width, vp->height); 
#else
	vp->numBytes = avpicture_get_size(PIX_FMT_RGBA, width, height); 
#endif

        } else {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
	vp->numBytes = avpicture_get_size(PIX_FMT_RGB565, vp->width, vp->height); 
#else
	vp->numBytes = avpicture_get_size(PIX_FMT_RGB565, width, height); 
#endif
        }

	vp->buffer = (uint8_t *) av_malloc(vp->numBytes*sizeof(uint8_t));

	if (!vp->pFrameRGB || !vp->buffer) {
		/* SDL allocates a buffer smaller than requested if the video
		 * overlay hardware is unable to support the requested size. */
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Error: the video system does not support an image\n"
				"size of %dx%d pixels. Try using -lowres or -vf \"scale=w:h\"\n"
				"to reduce the image size.\n", width, height);

		do_exit();

	} else {

                if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
			avpicture_fill( (AVPicture*)vp->pFrameRGB, vp->buffer, 
					PIX_FMT_RGBA,
					vp->width, vp->height);
#else
			avpicture_fill( (AVPicture*)vp->pFrameRGB, vp->buffer, 
					PIX_FMT_RGBA,
					width, height);
#endif


		} else {

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
			avpicture_fill( (AVPicture*)vp->pFrameRGB, vp->buffer, 
					PIX_FMT_RGB565,
					vp->width, vp->height);
#else
			avpicture_fill( (AVPicture*)vp->pFrameRGB, vp->buffer, 
					PIX_FMT_RGB565,
					width, height);
#endif
		}

		vp->allocated = 1;
	}

}

/**
 *
 * @param pts the dts of the pkt / pts of the frame and guessed if not known
 */
static int rgb_queue_picture(VideoState *is, AVFrame *pFrame, double pts, int64_t pos) 
{
	VideoPicture *vp;

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT QPB");

	// windex is set to 0 initially
	vp = &is->pictq[is->pictq_windex];

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
        if (vp->dst_width  != g_aspect_ratio_w ||
	    vp->dst_height != g_aspect_ratio_h) {
		vp->x = g_aspect_ratio_x;
		vp->y = g_aspect_ratio_y;
		vp->dst_width  = g_aspect_ratio_w;
		vp->dst_height = g_aspect_ratio_h;
	}

	if (!vp->pFrameRGB) 
        {
#else
	/* allocate or resize the buffer! */
	if (!vp->pFrameRGB ||
			vp->dst_width  != g_aspect_ratio_w ||
			vp->dst_height != g_aspect_ratio_h) {
#endif

		vp->allocated = 0;
		alloc_rgb_picture(is);

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT RQP ICB");

		if (vp->pix_fmt == PIX_FMT_YUV420P) {
			g_asm_yuv2rgb = 0;
			//g_asm_yuv2rgb = 1;
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "ASM YUV2RGB");
		} else {
			g_asm_yuv2rgb = 0;
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "FFmpeg YUV2RGB");
		}

#ifdef BROOV_PLAYER_SOURCE_WIDTH_HEIGHT
		is->img_convert_ctx = sws_getCachedContext(is->img_convert_ctx,
				vp->width, vp->height, vp->pix_fmt, vp->width,
				vp->height, dst_pix_fmt, sws_flags, 
				NULL, NULL, NULL);
#else
		is->img_convert_ctx = sws_getCachedContext(is->img_convert_ctx,
				vp->width, vp->height, vp->pix_fmt, vp->dst_width,
				vp->dst_height, dst_pix_fmt, sws_flags, 
				NULL, NULL, NULL);
#endif

		if (is->img_convert_ctx == NULL) {
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Unable to create a context");
			return -1;
		}

	}

	/* if the frame is not skipped, then display it */
	if (vp->pFrameRGB) {

		if (g_asm_yuv2rgb) {
#ifdef BROOV_X86_RELEASE
			sws_scale(is->img_convert_ctx, pFrame->data, pFrame->linesize,
					0, vp->height, vp->pFrameRGB->data, vp->pFrameRGB->linesize);

#else
                        if (g_video_yuv_rgb_asm == 0) {
			     sws_scale(is->img_convert_ctx, pFrame->data, pFrame->linesize,
					0, vp->height, vp->pFrameRGB->data, vp->pFrameRGB->linesize);
                        } else {
			if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB565)  {

				yuv420_2_rgb565(vp->pFrameRGB->data[0], 
						pFrame->data[0], 
						pFrame->data[1], 
						pFrame->data[2],
						is->video_st->codec->width, 
						is->video_st->codec->height, 
						pFrame->linesize[0],  // Y span
						pFrame->linesize[1],  // UV span Width / 2 
						(is->video_st->codec->width<<1),  //Dest width* bpp (width *2bytes)
						yuv2rgb565_table, 
						dither++);
			} else {
	                        yuv420_2_rgb8888(vp->pFrameRGB->data[0], 
						pFrame->data[0], 
						pFrame->data[1], 
						pFrame->data[2],
						is->video_st->codec->width, 
						is->video_st->codec->height, 
						pFrame->linesize[0],  // Y span
						pFrame->linesize[1],  // UV span Width / 2 
						(is->video_st->codec->width * 4),  //Dest width* bpp (width *4bytes)
						yuv2rgb565_table, 
						dither++);

                        }
                } // g_video_yuv_rgb_asm is true
#endif
		}

		else {
			sws_scale(is->img_convert_ctx, pFrame->data, pFrame->linesize,
					0, vp->height, vp->pFrameRGB->data, vp->pFrameRGB->linesize);
		}

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT RQP ICE");

		vp->pts = pts;
		vp->pos = pos;

		/* now we inform our display thread that we have a pic ready */
		if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
			is->pictq_windex = 0;
		}

		SDL_LockMutex(is->pictq_mutex);
		vp->target_clock= compute_target_time(vp->pts, is);
		is->pictq_size++;
		SDL_UnlockMutex(is->pictq_mutex);
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT QPE");
	return 0;
}

static int video_module_init(VideoState *is) 
{
	g_video_frame= avcodec_alloc_frame();

#ifdef BROOV_VIDEO_SKIP
	firstVideoPTSSet = false;
	firstVideoPTS = 0.0;
	lastVideoPTS = 0.0;

	//   1/framerate
	secondsPerFrame = 0;
	skipmode = 0;
	seenKeyFrameAfterCaughtUp = false;

	vlref_clock = current_time();

	secondsPerFrame = av_q2d(is->video_st->codec->time_base);
	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "secondsperFrame: %lf", secondsPerFrame);

#endif /* #ifdef BROOV_VIDEO_SKIP */

}

static int video_module_cleanup() 
{
	if (g_video_frame) {
		av_free(g_video_frame);
		g_video_frame = NULL;
	}
}

static int stream_component_open(VideoState *is, int stream_index) 
{
	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *codecCtx;
	AVCodec *codec;
	SDL_AudioSpec wanted_spec, spec;

	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream
	codecCtx = pFormatCtx->streams[stream_index]->codec;

	/* prepare audio output */
#ifdef BROOV_FFMPEG_OLD
	if (codecCtx->codec_type == CODEC_TYPE_AUDIO) {
#else
	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
#endif
		if (codecCtx->channels > 0) {
			codecCtx->request_channels = FFMIN(2, codecCtx->channels);
		} else {
			codecCtx->request_channels = 2;
		}
	}


	codec = avcodec_find_decoder(codecCtx->codec_id);

#if 1
	codecCtx->debug_mv = debug_mv;
	codecCtx->debug = g_debug;
	codecCtx->workaround_bugs = workaround_bugs;
	codecCtx->lowres = lowres;
	if(lowres) codecCtx->flags |= CODEC_FLAG_EMU_EDGE;
	codecCtx->idct_algo= idct;
	if(fast) codecCtx->flags2 |= CODEC_FLAG2_FAST;
	codecCtx->skip_frame= skip_frame;
	codecCtx->skip_idct= skip_idct;
	codecCtx->skip_loop_filter= skip_loop_filter;
#ifdef BROOV_FFMPEG_OLD
	codecCtx->error_recognition= error_recognition;
#endif
	codecCtx->error_concealment= error_concealment;
	//avcodec_thread_init(codecCtx, thread_count);

	//set_context_opts(codecCtx, avcodec_opts[codecCtx->codec_type], 0, codec);
#endif
	if(!codec || (avcodec_open(codecCtx, codec) < 0)) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	/* prepare audio output */
#ifdef BROOV_FFMPEG_OLD
	if(codecCtx->codec_type == CODEC_TYPE_AUDIO) {
#else
	if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
#endif

		// Set audio settings from codec info
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = codecCtx->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;
#ifdef BROOV_WITHOUT_AUDIO
#else
		if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
#endif
		is->audio_hw_buf_size = spec.size;

	}

	pFormatCtx->streams[stream_index]->discard = AVDISCARD_DEFAULT;

	switch(codecCtx->codec_type) {
#ifdef BROOV_FFMPEG_OLD
		case CODEC_TYPE_AUDIO:
#else
		case AVMEDIA_TYPE_AUDIO:
#endif
			is->audioStream = stream_index;
			is->audio_st = pFormatCtx->streams[stream_index];
			is->audio_buf_size = 0;
			is->audio_buf_index = 0;

			/* averaging filter for audio sync */
			is->audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));
			is->audio_diff_avg_count = 0;
			/* Correct audio only if larger error than this */
			is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / codecCtx->sample_rate;

			memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
			packet_queue_init(&is->audioq);
			SDL_PauseAudio(0);
			break;

#ifdef BROOV_FFMPEG_OLD
		case CODEC_TYPE_VIDEO:
#else
		case AVMEDIA_TYPE_VIDEO:
#endif
			is->videoStream = stream_index;
			is->video_st = pFormatCtx->streams[stream_index];

			is->frame_timer = (double)av_gettime() / 1000000.0;
			is->frame_last_delay = 40e-3;
			is->video_current_pts_time = av_gettime();

			video_module_init(is);

			break;

		default:
			break;
	}

}

static void stream_component_close(VideoState *is, int stream_index)
{
	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *avctx;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Component Close");
	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams)
		return;
	avctx = pFormatCtx->streams[stream_index]->codec;

	switch(avctx->codec_type) {
		case AVMEDIA_TYPE_AUDIO:
			packet_queue_abort(&is->audioq);

			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Before Close Audio");
			SDL_CloseAudio();
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "After Close Audio");

			packet_queue_end(&is->audioq);
			break;
		case AVMEDIA_TYPE_VIDEO:
#ifdef BROOV_VIDEO_THREAD
			packet_queue_abort(&is->videoq);
#endif
			/* note: we also signal this mutex to make sure we deblock the
			   video thread in all cases */
			SDL_LockMutex(is->pictq_mutex);
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);

#ifdef BROOV_VIDEO_THREAD
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Wait for Video Thread");
			SDL_WaitThread(is->video_tid, NULL);
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Wait for Video Thread Over");
			packet_queue_end(&is->videoq);
#endif
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Component Close Video Done");
			break;

		default:
			break;
	}

	pFormatCtx->streams[stream_index]->discard = AVDISCARD_ALL;
	avcodec_close(avctx);

	switch(avctx->codec_type) {
		case AVMEDIA_TYPE_AUDIO:
			is->audio_st = NULL;
			is->audioStream = -1;
			break;
		case AVMEDIA_TYPE_VIDEO:
			is->video_st = NULL;
			is->videoStream = -1;
			break;
		default:
			break;
	}

} /* stream_component_close() */

static void broov_update_aspect_ratio()
{
	float aspect_ratio;
	int w, h, x, y;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Inside Update Aspect Ratio X:%d Y:%d W:%d H:%d", g_aspect_ratio_x, g_aspect_ratio_y, g_aspect_ratio_w, g_aspect_ratio_h);

	if (!cur_stream) return ;

	if (!cur_stream->video_st) return ;

	if (cur_stream->video_st->codec->sample_aspect_ratio.num == 0) {
		aspect_ratio = 0;
	} else {
		aspect_ratio = av_q2d(cur_stream->video_st->codec->sample_aspect_ratio) *
			cur_stream->video_st->codec->width / cur_stream->video_st->codec->height;
	}

	if (g_aspect_ratio_type) {

		if (g_aspect_ratio_type == 1) {
			//4:3 format
			aspect_ratio = 4.0 / 3.0;
		}
		else if (g_aspect_ratio_type == 2) {
			//16:9 format
			aspect_ratio = 16.0 / 9.0;
		}
		else if (g_aspect_ratio_type == 3) {
			//Full screen
			aspect_ratio = (fs_screen_width * 1.0)/ fs_screen_height;
		}
	}

	if (aspect_ratio <= 0.0) {
		aspect_ratio = (float)cur_stream->video_st->codec->width /
			(float)cur_stream->video_st->codec->height;
	}

	h = screen->h;
	w = ((int)rint(h * aspect_ratio)) & -3;

	if (w > screen->w) {
		w = screen->w;
		h = ((int)rint(w / aspect_ratio)) & -3;
	}
	x = (screen->w - w) / 2;
	y = (screen->h - h) / 2;

	g_aspect_ratio_x = x;
	g_aspect_ratio_y = y;
	g_aspect_ratio_w = w;
	g_aspect_ratio_h = h;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Updated Aspect Ratio X:%d Y:%d W:%d H:%d", g_aspect_ratio_x, g_aspect_ratio_y, g_aspect_ratio_w, g_aspect_ratio_h);
}

#ifdef BROOV_FFMPEG_OLD
static int decode_interrupt_cb(void) 
{
	return (global_video_state && global_video_state->abort_request);
}
#else
static int decode_interrupt_cb(void *ctx) 
{
    //VideoState *is = ctx;
    //return is->abort_request;
    return (global_video_state && global_video_state->abort_request);
}
#endif

/* this thread gets the stream from the disk or the network */
static int decode_module_init(void *arg) 
{

	VideoState *is = (VideoState *)arg;
	AVFormatContext *pFormatCtx;

	int err;
	int ret = -1;

	int i;

	int video_index = -1;
	int audio_index = -1;


	is->videoStream=-1;
	is->audioStream=-1;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Inside decode_module_init");
	global_video_state = is;

#ifdef BROOV_FFMPEG_OLD
	// will interrupt blocking functions if we quit!
	url_set_interrupt_cb(decode_interrupt_cb);

	// Open video file
	err = av_open_input_file(&pFormatCtx, is->filename, NULL, 0, NULL);
#else
	pFormatCtx = avformat_alloc_context();
	pFormatCtx->interrupt_callback.callback = decode_interrupt_cb;
	pFormatCtx->interrupt_callback.opaque = is;

	// Open video file
        err = avformat_open_input(&pFormatCtx, is->filename, NULL, NULL);
#endif


	if (err < 0) {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "File open failed");
		ret = -1;
		goto decode_module_init_fail;

	}

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "File open successful");

	is->pFormatCtx = pFormatCtx;

	if (genpts) {
		pFormatCtx->flags |= AVFMT_FLAG_GENPTS;
	}

	// Retrieve stream information
	if (av_find_stream_info(pFormatCtx)<0) {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%s: could not find codec parameters\n", is->filename);

		ret = -1;
		goto decode_module_init_fail;
	}

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Finding streams successful");

	if(pFormatCtx->pb)
		pFormatCtx->pb->eof_reached= 0; //FIXME hack, ffplay maybe should not use url_feof() to test for the end

	if (seek_by_bytes<0) {
		seek_by_bytes= !!(pFormatCtx->iformat->flags & AVFMT_TS_DISCONT);
	}

	/* if seeking requested, we execute it */
	if (start_time != AV_NOPTS_VALUE) {
		int64_t timestamp;

		timestamp = start_time;
		/* add the stream start time */
		if (pFormatCtx->start_time != AV_NOPTS_VALUE)
			timestamp += pFormatCtx->start_time;
		ret = avformat_seek_file(pFormatCtx, -1, INT64_MIN, timestamp, INT64_MAX, 0);

		if (ret < 0) {
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%s: could not seek to position %0.3f\n",
					is->filename, (double)timestamp / AV_TIME_BASE);
		}
	}

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Before Calculate Duration");
	calculate_duration(pFormatCtx);

	// Find the first video stream
	for (i=0; i<pFormatCtx->nb_streams; i++) {

#ifdef BROOV_FFMPEG_OLD
		if (pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO &&
				video_index < 0) {
#else
		if (pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
				video_index < 0) {
#endif
			video_index=i;

			/* init subtitle here to avoid latency */
			if (g_show_subtitle) {
				is->use_sub = !subInit(is->filename, 
						1/av_q2d(pFormatCtx->streams[i]->time_base));
			} else {
				is->use_sub = 0;
			}
		}

#ifdef BROOV_FFMPEG_OLD
		if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_AUDIO &&
				audio_index < 0) {
#else
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
				audio_index < 0) {
#endif
			audio_index=i;
		}

	}

	if (audio_index >= 0) {
#ifdef BROOV_PLAYER_DEBUG
#else
		stream_component_open(is, audio_index);
#endif
	} else {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Audio not found");
	}

	if (video_index >= 0) {
		stream_component_open(is, video_index);

	}  else {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Video not found");
	}

	if (is->videoStream < 0 && is->audioStream < 0) {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%s: could not open codecs\n", is->filename);
		goto decode_module_init_fail;
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Before VideoSkip Init");

#ifdef BROOV_VIDEO_SKIP
	if (is->videoStream < 0) {
		//do not make audio callback to sleep when the file is having
		//only audio stream and no valid video stream is available

	} else {
		//calcutlate file framerate once for calculating frame rate lag.
		file_fps = calculate_file_fps(is);
		initialize_video_skip_variables();

	}
#endif
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "After VideoSkip Init");

#ifdef BROOV_FRAME_RATE
	init_frame_rate(0, BROOV_DEFAULT_NUM_FRAMES, BROOV_FRAME_DECODE_RATE);
	init_frame_rate(0, BROOV_DEFAULT_NUM_FRAMES, BROOV_FRAME_YUV_2_RGB_RATE);
	init_frame_rate(0, BROOV_DEFAULT_NUM_FRAMES, BROOV_FRAME_DISPLAY_RATE);
#endif

	broov_update_aspect_ratio();

	return 0;

decode_module_init_fail:

	return -1;	

} /* decode_module_init() */

static void stream_close(VideoState *is)
{
	VideoPicture *vp;
	int i;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Close");
	/* XXX: use a special url_shutdown call to abort parse cleanly */
	is->abort_request = 1;

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Close_WaitOver");

	/* free all pictures */
	for(i=0; i<VIDEO_PICTURE_QUEUE_SIZE; i++) {
		vp = &is->pictq[i];

		if (vp->pFrameRGB) {
			av_free(vp->pFrameRGB); vp->pFrameRGB=0;
		}
		if (vp->buffer) {
			av_free(vp->buffer); vp->buffer=0;
		}
		if (vp->display_rgb_text) {
			SDL_DestroyTexture(vp->display_rgb_text);
			vp->display_rgb_text=0;
		}
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Close_FreedImages");
	SDL_DestroyMutex(is->pictq_mutex);
	SDL_DestroyCond(is->pictq_cond);
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Close_FreedMutex");

	if (is->img_convert_ctx)
		sws_freeContext(is->img_convert_ctx);

	av_free(is);
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Stream Close_FreeDS");
}

int decode_module_clean_up(void *arg);
static void do_exit(void)
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Playerdo_exit");
	if (cur_stream) {
		cur_stream->abort_request = 1;
		decode_module_clean_up(cur_stream);
		stream_close(cur_stream);
		cur_stream = NULL;
	}

}

static void toggle_pause(void)
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Toggle Pause");
	if (cur_stream) {
		stream_pause(cur_stream);

	}
}

static void broov_play_clicked(void)
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Play Clicked");
	if (cur_stream) {
		if (cur_stream->paused) {
			stream_pause(cur_stream);
		}
	}
}

static void broov_pause_clicked(void)
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Pause Clicked");
	if (cur_stream) {
		if (!cur_stream->paused) {
			stream_pause(cur_stream);
		}
	}
}

int player_init(char *font_fname, int subtitle_show, int subtitle_font_size, int subtitle_encoding_type, int rgb565)
{
	// Register all formats and codecs
	av_register_all();

	//if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE)) 
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
	{
		return 2;
	}

	if (!try_to_set_best_video_mode(fs_screen_width, fs_screen_height, rgb565)) {
		return 3;
	}

	{
		int font_init = broov_font_init(font_fname, subtitle_font_size);
		if (font_init == 0) {
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "BroovFont init success");

		} else {
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "BroovFont init failed");
		}
	}

	{
		int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
		int initted = IMG_Init(img_flags);

		if (initted & img_flags != img_flags) {
			__android_log_print(ANDROID_LOG_INFO, "libSDL", "SDL_Image Initialization failure");
			return (4);
		}

	}

	if (subtitle_show == 0) {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Subtitle not to be shown");
		g_show_subtitle = 0;
	}
	else  {
		__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Subtitle to be shown");
		g_show_subtitle = 1;
	}

	g_subtitle_encoding_type = subtitle_encoding_type;

	broov_gui_init();

	return 0;
}

int player_duration()
{
	if (cur_stream) {
		if (cur_stream->seek_req_special) {

			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Return Seek Duration:%d", g_seek_duration);
			return g_seek_duration;
		}
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Return Current Duration:%d", g_current_duration);
	return g_current_duration;
}

int player_total_duration()
{
	return g_total_duration;
}

int player_play()
{
	SDL_Event event;
	event.type = FF_GUI_PLAY_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_pause()
{
	SDL_Event event;
	event.type = FF_GUI_PAUSE_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_forward()
{
	SDL_Event event;
	event.type = FF_GUI_FORWARD_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_rewind()
{
	SDL_Event event;
	event.type = FF_GUI_REWIND_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_prev()
{

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "PlayerPrev clicked");
	SDL_Event event;
	event.type = FF_GUI_PREV_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_next()
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "PlayerNext clicked");
	SDL_Event event;
	event.type = FF_GUI_NEXT_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int player_seek(int percentage)
{
	SDL_Event event;
	event.type = FF_GUI_SEEK_EVENT;
	event.user.data1 = (void*)percentage;
	SDL_PushEvent(&event);

	return 0;
}

int player_set_aspect_ratio(int aspect_ratio_type)
{
	SDL_Event event;
	event.type = FF_GUI_ASPECT_RATIO_EVENT;
	event.user.data1 = (void*)aspect_ratio_type;
	SDL_PushEvent(&event);

	return 0;
}

int player_exit()
{
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "PlayerExit called");
	TTF_Quit();  /* remember to quit SDL_ttf */
	free_font(); /* remember to free any loaded font and glyph cache */
	subFree();

	broov_gui_clean();

	SDL_Quit();

	SDL_ANDROID_CallJavaExitFromNativePlayerView();
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "PlayerExit done");

	return 0;
}

int process_a_ui_event(VideoState *is)
{
	int ret_value;
	SDL_Event event;

	// Return if there is no events in UI queue
	ret_value = SDL_PollEvent(&event);
	if (!ret_value) return 0;

	{
		double incr, pos;
		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "event.type:%d", event.type);
		switch(event.type) {
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym) {
					case SDLK_p:
					case SDLK_SPACE:
					case SDLK_UP:
					case SDLK_DOWN:
						toggle_pause();
						break;

					case SDLK_d:
						g_debug = 1;
						break;
					case SDLK_z:
					case SDLK_x:
						audio_clock_delta += (event.key.keysym.sym == SDLK_z) ? -0.1 : 0.1;
						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Audio sync correction : %+.1fs", audio_clock_delta);
						break;

					case SDLK_LEFT:
						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek left 10s");
						incr = -10.0;
						goto do_seek;
					case SDLK_RIGHT:
						incr = 10.0;
						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek right");
						goto do_seek;
do_seek:

						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Do Seek");
						if (cur_stream) {
							if (seek_by_bytes) {
								if (cur_stream->video_st >= 0 && cur_stream->video_current_pos>=0){
									pos= cur_stream->video_current_pos;
								}else if(cur_stream->audio_st >= 0 && cur_stream->audio_pkt.pos>=0){
									pos= cur_stream->audio_pkt.pos;
								}else {
#ifdef BROOV_FFMPEG_OLD
									pos = url_ftell(cur_stream->pFormatCtx->pb);
#else
									pos = avio_tell(cur_stream->pFormatCtx->pb);
#endif
								}
								if (cur_stream->pFormatCtx->bit_rate)
									incr *= cur_stream->pFormatCtx->bit_rate / 8.0;
								else
									incr *= 180000.0;
								pos += incr;
								stream_seek(cur_stream, pos, incr, 1);
							} else {
								pos = get_master_clock(cur_stream);
								pos += incr;
								stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
							}
						}
						break;

					default:
						break;

				}
				break;
			case SDL_ACTIVEEVENT:
				{
					//if (event.active.gain == 0 && active.state & SDL_APPACTIVE) {
					//    __android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VideoResize event received W:%d, H:%d", event.resize.w, event.resize.h);
					//    try_to_set_best_video_mode(event.resize.w, event.resize.h);
					//}
				}
				break;
			case SDL_VIDEORESIZE:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VideoResize event received W:%d, H:%d", event.resize.w, event.resize.h);
                                        if (g_video_output_rgb_type == VIDEO_OUTPUT_RGB8888) {
						try_to_set_best_video_mode(event.resize.w, event.resize.h, 0);
					} else {
						try_to_set_best_video_mode(event.resize.w, event.resize.h, 1);
					}
				}
				break;

			case SDL_FINGERDOWN:
				//toggle_pause();
				break;

			case SDL_FINGERMOTION:
				//toggle_pause();
				break;

			case FF_QUIT_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "FF_QUIT_EVENT received");
					ret_value = BROOV_FF_QUIT; 
					is->abort_request = 1;
					do_exit();
					goto player_done;
					break;
				}
			case SDL_QUIT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "SDL_QUIT received");
					ret_value = BROOV_SDL_QUIT; 
					is->abort_request = 1;
					do_exit();
					goto player_done;
					break;
				}

			case FF_GUI_PLAY_EVENT:
				{
					broov_pause_clicked();
					break;
				}

			case FF_GUI_PAUSE_EVENT:
				{
					broov_play_clicked();
					break;
				}

			case FF_GUI_FORWARD_EVENT:
				{
					incr = 10.0;
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek right 10s");
					goto do_seek;
				}

			case FF_GUI_REWIND_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek left 10s");
					incr = -10.0;
					goto do_seek;
				}
			case FF_GUI_PREV_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Prev Button Received");
					ret_value = BROOV_PREV_BUTTON_CLICKED; 
					is->abort_request = 1;
					do_exit();
					goto player_done;
					break;
				}

			case FF_GUI_NEXT_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Next Button Received");
					ret_value = BROOV_NEXT_BUTTON_CLICKED; 
					is->abort_request = 1;
					do_exit();
					goto player_done;
					break;
				}

			case FF_GUI_ASPECT_RATIO_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Aspect Ratio Received:%d", event.user.data1);
					{ 
						int aspect_ratio_type = (int)event.user.data1; 
						if (aspect_ratio_type == 0) {
							g_aspect_ratio_type = 0;
						}
						else if (aspect_ratio_type == 1) {
							g_aspect_ratio_type = 1; // 4:3
						}
						else if (aspect_ratio_type == 2) {
							g_aspect_ratio_type = 2; // 16:9
						}
						else if (aspect_ratio_type == 3) {
							g_aspect_ratio_type = 3; // full screen
						}
						broov_update_aspect_ratio();
						clear_screen(screen);
						clear_screen(screen);
					}

					break;
				}


			case FF_GUI_SEEK_EVENT:
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Seek Value Received:%d", event.user.data1);
					if (g_total_duration) {
						int percent=(int)event.user.data1;
						double current_percentile = (percent/10.0) ; 
						double per_percentile_num_of_secs = (double) (g_total_duration/100.0);
						incr = (double)(current_percentile * per_percentile_num_of_secs);
						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "CurrentPercentileSeek:%lf PerPercentileSeek:%lf, DurationToMove:%lf", current_percentile, per_percentile_num_of_secs, incr);
						g_seek_duration = incr;
						goto do_seek_special;
					}

					break;

				}

			case FF_ALLOC_RGB_EVENT:
				__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Alloc RGB Frame");
				alloc_rgb_picture(event.user.data1);
				break;

			case FF_REFRESH_RGB_EVENT:
				video_refresh_rgb_timer(event.user.data1);
				cur_stream->refresh=0;
				break;

			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEMOTION:
				{
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				{
				}
				break;

do_seek_special:
				__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Do Seek Special");
				if (cur_stream) {
					stream_seek_special(cur_stream, (int64_t)(incr * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
				}

				break;



			default:
				break;
		}
	}
player_done:
	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "End of UI events processing");

	return ret_value;
}

void main_module_cleanup(int audio_file_type)
{
#ifndef BROOV_GUI_NO_LOADING_SCREEN
	if (!audio_file_type) {
		clear_screen(screen);
		clear_screen(screen);
	}
#endif

	g_total_duration=0;
	g_current_duration=0;
	g_seek_duration=0;

	//Free the allocated subtitles for the current video, if any
	subFree();
	broov_gui_clean_audio_image();

}

int decode_module_clean_up(void *arg)
{
	VideoState *is = (VideoState *)arg;
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "DecodeModule CleanUp");

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Waiting for DecodeThread Exit");
	SDL_WaitThread(is->parse_tid, NULL);
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Waiting for DecodeThread Over");

	/* disable interrupting */
	global_video_state = NULL;

	/* close each stream */
	if (is->audioStream >= 0)
		stream_component_close(is, is->audioStream);
	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "DecodeModule After Audio Stream Close");

	if (is->videoStream >= 0)
		stream_component_close(is, is->videoStream);

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "DecodeModule After Video Stream Close");


	if (is->pFormatCtx) {
		av_close_input_file(is->pFormatCtx);
		is->pFormatCtx = NULL; /* safety */
	}
#ifdef BROOV_FFMPEG_OLD
	url_set_interrupt_cb(NULL);
#endif

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "DecodeModule CleanUp End");

}
#ifdef BROOV_VIDEO_THREAD
static int video_thread(void *arg)
{
	VideoState *is = (VideoState*) arg;
	AVPacket *packet;
	int i;

#ifdef BROOV_FRAME_RATE
	double st=0;
	double ct=0;
	double qt=0;
	st = ct = current_time();
#endif

	for(;;) {

		if (is->abort_request) { break; }

		if (is->paused && !is->videoq.abort_request) {
			SDL_Delay(10);
			continue;
		}


		if (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE) {
			//Wait for processing of pictq before writing
			//further frames
			//usleep(5000); //Sleep for 10ms
			usleep(10000); //Sleep for 10ms
			//SDL_Delay(10); 
			continue;
		}
		AVPacket pkt;

		if (packet_queue_get(&is->videoq, &pkt, 1) < 0) {
			goto the_end;
		}

		packet=&pkt;

		if (packet->data == flush_pkt.data) {
			avcodec_flush_buffers(is->video_st->codec);

			SDL_LockMutex(is->pictq_mutex);
			//Make sure there are no long delay timers (ideally we should just flush the que but thats harder)
			for (i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; i++) {
				is->pictq[i].target_clock= 0;
			}
			while (is->pictq_size && !is->videoq.abort_request) {
				SDL_CondWait(is->pictq_cond, is->pictq_mutex);
			}
			is->video_current_pos = -1;
			SDL_UnlockMutex(is->pictq_mutex);

			is->frame_last_pts = AV_NOPTS_VALUE;
			is->frame_last_delay = 0;
			is->frame_timer = (double)av_gettime() / 1000000.0;
			continue;
		}

		{
			register int frameFinished=0;

			/* NOTE: ipts is the PTS of the _first_ picture beginning in
			   this packet, if any */
			is->video_st->codec->reordered_opaque= packet->pts;
			avcodec_decode_video2(is->video_st->codec, g_video_frame, &frameFinished, packet);

                        if (g_last_skip_type == AVDISCARD_BIDIR) {
                            g_alternate_skip_change= !g_alternate_skip_change;
                            if (g_alternate_skip_change) {
                                is->video_st->codec->skip_frame = AVDISCARD_BIDIR;
                            } else {
                                is->video_st->codec->skip_frame = AVDISCARD_DEFAULT;
                            }
                        }

			if (frameFinished) {

#ifdef BROOV_FRAME_RATE
				ct = current_time();
				get_average_of_N((ct-st), BROOV_FRAME_DECODE_RATE); 
				st = ct;
#endif

#ifdef BROOV_VIDEO_SKIP	
				if (g_skip_frames) {
					if (skip_now(is)) {
						double start_output_picture2;
						double end_output_picture2;	
						//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT BOP");
						start_output_picture2 = current_time();

						{
							double        pts;
							double        frame_delay;
							if (packet->dts == AV_NOPTS_VALUE) {
								//pts = 0;
								pts = is->video_clock;
							} else {
								pts = packet->dts * av_q2d(is->video_st->time_base);
								/* update video clock with pts, if present */
								is->video_clock = pts;
							}

							/* update video clock for next frame */
							frame_delay = av_q2d(is->video_st->codec->time_base);

							/* for MPEG2, the frame can be repeated, so we update the
							   clock accordingly */
							frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
							is->video_clock += frame_delay;

							rgb_queue_picture(is, g_video_frame, pts, packet->pos);

						}

						end_output_picture2 = current_time();

						calculate_avg_output_time(start_output_picture2, end_output_picture2);
					}else{ 	  
						skipped= true;		
					}
				}
				else {
					double        pts;
					double        frame_delay;
					if (packet->dts == AV_NOPTS_VALUE) {
						//pts = 0;
						pts = is->video_clock;
					} else {
						pts = packet->dts * av_q2d(is->video_st->time_base);
						/* update video clock with pts, if present */
						is->video_clock = pts;
					}

					/* update video clock for next frame */
					frame_delay = av_q2d(is->video_st->codec->time_base);

					/* for MPEG2, the frame can be repeated, so we update the
					   clock accordingly */
					frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
					is->video_clock += frame_delay;

					rgb_queue_picture(is, g_video_frame, pts, packet->pos);
				}

#else
				double        pts;
				double        frame_delay;
				if (packet->dts == AV_NOPTS_VALUE) {
					//pts = 0;
					pts = is->video_clock;
				} else {
					pts = packet->dts * av_q2d(is->video_st->time_base);
					/* update video clock with pts, if present */
					is->video_clock = pts;
				}

				/* update video clock for next frame */
				frame_delay = av_q2d(is->video_st->codec->time_base);

				/* for MPEG2, the frame can be repeated, so we update the
				   clock accordingly */
				frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
				is->video_clock += frame_delay;

				rgb_queue_picture(is, g_video_frame, pts, packet->pos);

#endif /* #ifdef BROOV_VIDEO_SKIP */

#ifdef BROOV_FRAME_RATE
				qt = current_time();
				get_average_of_N( (qt-st), BROOV_FRAME_YUV_2_RGB_RATE);
				if (get_frames_to_skip()) {
                                    is->video_st->codec->skip_frame = AVDISCARD_DEFAULT;
                                    g_last_skip_type = AVDISCARD_DEFAULT;
                                } else {
                                    is->video_st->codec->skip_frame = AVDISCARD_BIDIR;
                                    g_last_skip_type = AVDISCARD_BIDIR;
                                }
#endif
			}
		}

		av_free_packet(&pkt);

	}

the_end:
	return 0;
}
#endif

static int decode_thread(void *arg)
{
	VideoState *is = (VideoState*) arg;

	AVPacket pkt1, *packet = &pkt1;
	AVFormatContext *pFormatCtx;
	int ret;

	int ret_value = 0;
	int vid_ret_value = 0;
	pFormatCtx = is->pFormatCtx;

	while (1) {

		if (is->abort_request) { ret_value=0; break; }

		if (is->paused) { 
			usleep(10000); //Sleep for 10ms
			//SDL_Delay(10); 
			continue; 
		}

#ifdef BROOV_VIDEO_THREAD
#else
		if (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE) {

			//Wait for processing of pictq before writing
			//further frames
			usleep(5000); //Sleep for 10ms
			//usleep(10000); //Sleep for 10ms
			//SDL_Delay(10); 
			continue;
		}
#endif

		if (is->audioq.size > MAX_AUDIOQ_SIZE) {
			/* wait 10 ms */
			usleep(5000); //Sleep for 10ms
			//usleep(10000); //Sleep for 10ms
			//SDL_Delay(10); 
			continue;
		}

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Decode Module Processing ");
		// Read an audio/video packet from file stream
		// and write to queue
		// decode module..Decode the frame
		if (is->paused != is->last_paused) {
			is->last_paused = is->paused;
			if (is->paused)
				is->read_pause_return= av_read_pause(pFormatCtx);
			else
				av_read_play(pFormatCtx);
		}

		// seek stuff goes here
		if (is->seek_req) {
			if (is->seek_req_special) {
				/* add the stream start time */
				if (is->pFormatCtx->start_time != AV_NOPTS_VALUE)
					is->seek_pos+= pFormatCtx->start_time;
				ret = avformat_seek_file(is->pFormatCtx, -1, INT64_MIN, is->seek_pos, INT64_MAX, 0);
				if (!(ret<0)) {
					g_current_duration = g_seek_duration;
					g_seek_success = 1;
				}
				is->seek_req_special=0;
			}
			else {
				int64_t seek_target = is->seek_pos;
				int64_t seek_min= is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
				int64_t seek_max= is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
				//FIXME the +-2 is due to rounding being not done in the correct direction in generation
				//of the seek_pos/seek_rel variables
				ret = avformat_seek_file(is->pFormatCtx, -1, seek_min, seek_target, seek_max, is->seek_flags);
			}

			if (ret < 0) {
				__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%s: error while seeking\n", is->pFormatCtx->filename);
				is->seek_req = 0;
				g_eof= 0;
			} else {
				if (is->audioStream >= 0) {
					packet_queue_flush(&is->audioq);
					packet_queue_put(&is->audioq, &flush_pkt);
				}

				if (is->videoStream >= 0) {
#ifdef BROOV_VIDEO_THREAD
					packet_queue_flush(&is->videoq);
					packet_queue_put(&is->videoq, &flush_pkt);
#else
					int i;
					avcodec_flush_buffers(is->video_st->codec);

					SDL_LockMutex(is->pictq_mutex);

					//Make sure there are no long delay timers (ideally we should just flush the que but thats harder)
					for(i=0; i<VIDEO_PICTURE_QUEUE_SIZE; i++){
						is->pictq[i].target_clock= 0;
					}

					while (is->pictq_size && !is->abort_request) {
						SDL_CondWait(is->pictq_cond, is->pictq_mutex);
					}
					is->video_current_pos= -1;
					SDL_UnlockMutex(is->pictq_mutex);

					is->frame_last_pts= AV_NOPTS_VALUE;
					is->frame_last_delay = 0;
					is->frame_timer = (double)av_gettime() / 1000000.0;
#endif
				}

				is->seek_req = 0;
				g_eof= 0;
			}
		} // if is seek requested

#ifdef BROOV_VIDEO_THREAD
		if (in_sync) {
			if (is->videoq.size > BROOV_VIDEO_MAX_BUFFER_SIZE)
			        //if (is->videoq.size > 3048576)
				//if (is->videoq.size > 1MB)
			{
				in_sync = false;
				SDL_Delay(10);
				continue;	
				//return 1; 
			}
		} else {
			// __android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "OUT_OF_SYNC videoq.nb_packets:%d", is->videoq.nb_packets);  
			if (is->videoq.size < BROOV_VIDEO_MIN_BUFFER_SIZE)
			        //if (is->videoq.size < 256000)
				//if (is->videoq.size < 100KB)
			{
				in_sync=true;
				SDL_Delay(10);
				continue;
				//return 2;
			}
			else{
				SDL_Delay(10);
				continue;
				//return 3;
			}
		}

		//if ((is->audioq.size + is->videoq.size)> (1024*1024))
		//if ((is->audioq.size + is->videoq.size) > (4048576))

		if ((is->audioq.size + is->videoq.size) > BROOV_TOTAL_MAX_BUFFER_SIZE)
		{
			/* wait 10 ms */
			SDL_Delay(10);
			continue;
			//return 4;
		}

#endif
		if (g_eof) {
#ifdef BROOV_VIDEO_THREAD
			if (is->videoStream >= 0) {
				av_init_packet(packet);
				packet->data=NULL;
				packet->size=0;
				packet->stream_index= is->videoStream;
				packet_queue_put(&is->videoq, packet);
			}
#endif
			usleep(10000); //Sleep for 10ms
			//SDL_Delay(10); 

#ifdef BROOV_VIDEO_THREAD
			if ((is->audioq.size + is->videoq.size) ==0)
#else
				if (is->audioq.size ==0) 
#endif
				{
					__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Decode Thread EOF");
					if (g_autoexit) {
						ret=AVERROR_EOF;
						goto decode_module_fail;
					} 
					else if (g_loop==1) {
						__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Looping the file");
						stream_seek(cur_stream, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
						g_eof=0;
					}
				}
			continue;
			//return 5;
		}

		ret = av_read_frame(pFormatCtx, packet);

		if (ret < 0)  {

			if (ret == AVERROR_EOF || url_feof(pFormatCtx->pb))
				g_eof=1;
#ifdef BROOV_FFMPEG_OLD
			if (url_ferror(pFormatCtx->pb)) {
				goto decode_after_read;
			}
#else
			if (pFormatCtx->pb && pFormatCtx->pb->error) {
				goto decode_after_read;
                        }
#endif
			usleep(10000); //Sleep for 100ms

			//SDL_Delay(100); /* no error; wait for user input */
			continue;
			//return 6;
		}

		// Is this a packet from the video stream?
		if (packet->stream_index == is->videoStream) {
#ifdef BROOV_VIDEO_THREAD

#ifdef BROOV_ONLY_AUDIO
			av_free_packet(packet);
#else
			packet_queue_put(&is->videoq, packet);
#endif /* #ifdef BROOV_ONLY_AUDIO */

#else
			register int frameFinished=0;

			/* NOTE: ipts is the PTS of the _first_ picture beginning in
			   this packet, if any */
			is->video_st->codec->reordered_opaque= packet->pts;
			avcodec_decode_video2(is->video_st->codec, g_video_frame, &frameFinished, packet);
			if (frameFinished) {

#ifdef BROOV_VIDEO_SKIP	
				if (g_skip_frames) {
					if (skip_now(is)) {
						double start_output_picture2;
						double end_output_picture2;	
						//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VT BOP");
						start_output_picture2 = current_time();

						{
							double        pts;
							double        frame_delay;
							if (packet->dts == AV_NOPTS_VALUE) {
								//pts = 0;
								pts = is->video_clock;
							} else {
								pts = packet->dts * av_q2d(is->video_st->time_base);
								/* update video clock with pts, if present */
								is->video_clock = pts;
							}

							/* update video clock for next frame */
							frame_delay = av_q2d(is->video_st->codec->time_base);

							/* for MPEG2, the frame can be repeated, so we update the
							   clock accordingly */
							frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
							is->video_clock += frame_delay;

							rgb_queue_picture(is, g_video_frame, pts, packet->pos);

						}

						end_output_picture2 = current_time();

						calculate_avg_output_time(start_output_picture2, end_output_picture2);
					}else{ 	  
						skipped= true;		
					}
				}
				else {
					double        pts;
					double        frame_delay;
					if (packet->dts == AV_NOPTS_VALUE) {
						//pts = 0;
						pts = is->video_clock;
					} else {
						pts = packet->dts * av_q2d(is->video_st->time_base);
						/* update video clock with pts, if present */
						is->video_clock = pts;
					}

					/* update video clock for next frame */
					frame_delay = av_q2d(is->video_st->codec->time_base);

					/* for MPEG2, the frame can be repeated, so we update the
					   clock accordingly */
					frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
					is->video_clock += frame_delay;

					rgb_queue_picture(is, g_video_frame, pts, packet->pos);
				}

#else
				double        pts;
				double        frame_delay;
				if (packet->dts == AV_NOPTS_VALUE) {
					//pts = 0;
					pts = is->video_clock;
				} else {
					pts = packet->dts * av_q2d(is->video_st->time_base);
					/* update video clock with pts, if present */
					is->video_clock = pts;
				}

				/* update video clock for next frame */
				frame_delay = av_q2d(is->video_st->codec->time_base);

				/* for MPEG2, the frame can be repeated, so we update the
				   clock accordingly */
				frame_delay += g_video_frame->repeat_pict * (frame_delay * 0.5);
				is->video_clock += frame_delay;

				rgb_queue_picture(is, g_video_frame, pts, packet->pos);

#endif /* #ifdef BROOV_VIDEO_SKIP */

			}
			av_free_packet(packet);

#endif /* BROOV_VIDEO_THREAD */

		} else if (packet->stream_index == is->audioStream) {
#ifdef BROOV_WITHOUT_AUDIO
			av_free_packet(packet);
#else
			packet_queue_put(&is->audioq, packet);
#endif
		}
		else {
			av_free_packet(packet);
		}

decode_after_read:
		continue;

decode_module_fail:
		//Thread shall quit now
		ret_value = -1;
		break;

	}

	//Ask the player_main thread to quit 
	if (ret_value != 0) {
		SDL_Event event;

		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}

}

int player_main(int argc, char *argv[], 
		int loop_after_play, int audio_file_type, int skip_frames, int rgb565, int yuv_rgb_asm,
                int skip_bidir_frames, int vqueue_size_min, int vqueue_size_max, int total_queue_size, int audio_queue_size)

{
	VideoState      *is;
	int ret_value = 0;
	int ui_scheduler = 0;
	int decode_scheduler = 0;

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Loop:%d FileType:%d SkipFrames:%d RGB565:%d YUV_RGB_ASM:%d", loop_after_play, audio_file_type, skip_frames, rgb565, yuv_rgb_asm);

#ifdef BROOV_PLAYER_PROFILING
	//monstartup("application.so");
	monstartup("ffmpeg.so");
	//monstartup("sdl.so");
#endif

	if (loop_after_play == 0) {
		g_autoexit = 1;
		g_loop = 0;
	} else if (loop_after_play == 1) {
		g_autoexit = 0;
		g_loop = 1;
	}

	g_aspect_ratio_type = 0;
	g_skip_frames = skip_frames;

        if (rgb565 == 1) { g_video_output_rgb_type = VIDEO_OUTPUT_RGB565; } 
        else { g_video_output_rgb_type = VIDEO_OUTPUT_RGB8888; }

        if (yuv_rgb_asm == 1) { g_video_yuv_rgb_asm = 1; } else { g_video_yuv_rgb_asm = 0; }

        if (skip_bidir_frames) { g_last_skip_type = AVDISCARD_BIDIR; }
        if (vqueue_size_min) { BROOV_VIDEO_MIN_BUFFER_SIZE = vqueue_size_min; }
        if (vqueue_size_max) { BROOV_VIDEO_MAX_BUFFER_SIZE = vqueue_size_max; }
        if (total_queue_size) { BROOV_TOTAL_MAX_BUFFER_SIZE= total_queue_size; }
        if (audio_queue_size) { MAX_AUDIOQ_SIZE = audio_queue_size; }

	g_eof=0;
	g_total_duration=0;
	g_current_duration=0;
	g_seek_duration=0;

	if (audio_file_type == 1) {
		g_audio_file_type = 1;
		broov_gui_init_audio_image(0);
	} else {
		// the file is a video file
		// load a black background screen
		g_audio_file_type = 0;

#ifndef BROOV_GUI_NO_LOADING_SCREEN
		clear_screen(screen);
		clear_screen(screen);
		broov_gui_init_audio_image(BROOV_LOADING_IMAGE);
		broov_gui_show_video_image(screen);
#endif
	}

	broov_gui_init_ds();

	seek_by_bytes = -1;
	start_time = AV_NOPTS_VALUE;
	duration   = AV_NOPTS_VALUE;

	is = (VideoState *)av_mallocz(sizeof(VideoState));

	strncpy(is->filename, argv[1], FILE_NAME_SIZE);

	/* start video display */
	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();

	is->av_sync_type = av_sync_type;
	is->abort_request = 0;

	cur_stream = is;

	if (decode_module_init(is) < 0) {
		goto main_module_clean_up;
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Decode Thread Creation Successful");

	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t *)"FLUSH";

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Flush Pkt Init Successful");

	is->parse_tid = SDL_CreateThread(decode_thread, is);
	if (!is->parse_tid) {
		av_free(is);
		return NULL;
	}

#ifdef BROOV_VIDEO_THREAD
	if (is->video_st) {
		packet_queue_init(&is->videoq);
		is->video_tid = SDL_CreateThread(video_thread, is);
		if (!is->video_tid) {
			__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "VideoThread Creation Failed");
			return NULL;
		}
	}
#endif

	while (1) {

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "UI Events Processing ");

		if (is->abort_request) { ret_value=0; break; }

		//Schedule UI events to be processed once every X% times
		// in this loop
		//if ((ui_scheduler%64) == 0)
		{

			//Process all available UI events
			do {
				ret_value = process_a_ui_event(is);

				if (1 == ret_value) continue;

				break;

			} while(1);

			if (ret_value ==BROOV_PREV_BUTTON_CLICKED ||
					ret_value ==BROOV_NEXT_BUTTON_CLICKED) {
				break;
			}
			if (ret_value ==BROOV_FF_QUIT ||
					ret_value ==BROOV_SDL_QUIT) {
				break;
			}
		}
		//ui_scheduler++;

		if (is->paused) { SDL_Delay(10); continue; }

		if (is->video_st && is->pictq_size) {
			video_refresh_rgb_timer(is);
		}
		else if (is->video_st) {
			SDL_Delay(10); //Sleep for 10 ms
		}
		else { 
			//Update the audio stream duration
			ULONG dur = (ULONG) get_master_clock(is);

			// update the value, only if a value change happens
			if (dur != g_current_duration) {
				broov_set_current_duration(dur);
			}

			//This is an audio stream without video
			SDL_Delay(100); //Sleep for 100 ms 
		}

	} // video/audio file processing loop

main_module_clean_up:

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "MainThread Exit");

	video_module_cleanup();
	do_exit(); // decode module is cleaned inside this
	main_module_cleanup(audio_file_type);

	__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "Player is Quit now()");

#ifdef BROOV_PLAYER_PROFILING
	moncleanup();
#endif

	return ret_value;
}

#ifdef BROOV_VIDEO_SKIP

/*
 * Method to calculate frames per second for the skip_now method logic
 * This implements same method as of calculate_frames_per_second 
 * but it is called in the skip_now logic only for getting the 
 * frame_rate of the file playing
 *
 * Updates global variable player_fps_tempin 
 * The numberofskipped variable is incremented for the number of times skipped
 *
 */
static void calculate_frames_per_second_internal()
{
	static double stin=0;
	static double ctin=0;
	static double secondsin=0;
	static int framesin=0;
	static int numberofskipped =0;

	if (stin == 0) {
		stin = ctin = secondsin = current_time();
	}

	{
		double ttin = current_time();		

		if (skipped){
			//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "ttin: %lf ", ttin);
			//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "avg_outputtime_missed: %lf ", avg_outputtime_missed);
			numberofskipped = numberofskipped+1;

			//ttin = ttin + avg_outputtime_missed;
			//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "ttin + avg_outputtime_missed: %lf ", ttin);
			skipped = false;
		}

		double dtin; //= tt - ct; //elapsed time
		ctin = ttin; //current time
		framesin++;
		dtin = ctin - secondsin;

		//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer internal", "ttin: %lf, dtin: %lf, ctin: %lf, secondsin: %lf", ttin, dtin, ctin, secondsin );
		if (dtin >= 2.0)
		{	
			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "dtin: %lf", dtin);
			dtin  = dtin + (numberofskipped * avg_outputtime_missed);
			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "dtin: %lf, numberofskipped : %d, avg_outputtime_missed : %lf", dtin, numberofskipped, avg_outputtime_missed);

			player_fps_tempin = framesin/dtin;
			//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "NEW_player_Fps: %lf", player_fps_tempin);
			//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "%d frames in %3.1f seconds = %6.3f FPS", framesin, dtin, player_fpsin);
			secondsin = ctin;
			framesin = 0;
			numberofskipped = 0;
		}
	}
}

static int average_number_for_output_time = 0;
static double output_picture_time = 0.0;

static void calculate_avg_output_time(double start_output_picture2, double end_output_picture2)
{
	double output_pic_time1 = end_output_picture2 - start_output_picture2;

	//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "output_pic_time1 : %lf", output_pic_time1);
	output_picture_time = output_picture_time + end_output_picture2 - start_output_picture2;

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "output_picture_time : %lf, start_output_picture2 : %lf, end_output_picture2 : %lf", output_picture_time, start_output_picture2, end_output_picture2);
	average_number_for_output_time = average_number_for_output_time+1;

	if(average_number_for_output_time>=5){
		avg_outputtime_missed = output_picture_time/average_number_for_output_time;
		output_picture_time = 0;
		average_number_for_output_time=0;
	}

	//__android_log_print(ANDROID_LOG_INFO, "BroovPlayer", "avg_outputtime_missed : %lf", avg_outputtime_missed);
	//avg_outputtime_missed = 20.0;
}

static double calculate_file_fps(VideoState *is)
{
	double secondsPerFrame;
	double file_fps_in;
	int rfps      = is->video_st->r_frame_rate.num;
	int rfps_base = is->video_st->r_frame_rate.den;

	secondsPerFrame = av_q2d( is->video_st->codec->time_base );
	file_fps_in= (1.0/secondsPerFrame);
	if (is->video_st->codec->time_base.den != rfps*is->video_st->codec->ticks_per_frame || is->video_st->codec->time_base.num != rfps_base) {
		file_fps_in =((float)rfps / rfps_base);
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "codec rfps : %lf", file_fps);
	}
	return file_fps_in;
}

static double calculate_video_lag(double videoTime, double systemMasterClock)
{
	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "vlref_clock : %lf, systemMasterClock : %lf",vlref_clock, systemMasterClock);	
	double actualtime_fromstart = (systemMasterClock - vlref_clock);
	double videotimelag =  actualtime_fromstart - videoTime;
	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "time_fromstart : %lf, videotimelag : %lf", actualtime_fromstart, videotimelag);	
	double video_fps_lag = videotimelag * file_fps;
	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "video_fps_lag : %lf", video_fps_lag);	
	video_lag_err = (video_fps_lag);
	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "video_fps_lag per second : %lf", (video_lag_err/60));	
}

void initialize_video_skip_variables()
{
	skipafternumberofframes=2;
	skipafternumberofframessmall=3;
	alternateskip = true;
	skipnumberoftimes1 = 2;
	skipnumberoftimes2 = 3;
	skipnumberoftimeslarge1 = 5;
	skipnumberoftimeslarge2 = 7;

	average_number_for_output_time=0;
	avg_outputtime_missed=0.0;
	output_picture_time=0.0;
	skipped = false;

}		

/*********************************************************************************************************
 * skip_now method returns 
 *
 *    0 if false
 *    1 if true 
 * 
 * Decision maker to skip the frame
 *
 * Logic:
 * 1. Find the frame rate for file - file_fps
 * 2. Calculate current frame rate using calculate_frames_per_second_internal() and stored in player_fps_tempin
 * 3. Calculate fps lag i.e difference between current frame rate and file frame rate - fps_lag
 * 4. Skip mode set as per fps_lag value. i.e as lag increases skip rate increases(max:skipping 6 frames in 7)
 * 5. if fps_lag is negative then there is no need for skip and that case is handled in video refresh timer
 *
 *********************************************************************************************************/
static int skip_now(VideoState *is)
{
#ifdef TEST_SKIP_LOGIC
	if (skipnumberoftimes1>0){
		skipnumberoftimes1--;
		return 0;
	}else if(skipnumberoftimes1<=0){
		skipnumberoftimes1=2;
		return 1;
	}
#endif
	//values declared in static for first time initialisation and declaration
	calculate_frames_per_second_internal();

#ifdef BROOV_FRAME_SKIP_DEBUG
	//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "File_fps : %lf", file_fps);
	//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "player_fps_tempin : %lf", player_fps_tempin);
#endif

	//double fps_lag = file_fps - (player_fps_tempin - video_lag_err);
	int fps_lag = file_fps -  (int)player_fps_tempin ;

	if (player_fps_tempin<1){
		//return 1;
		fps_lag =13;
	}

	//fps_lag = fps_lag + 2;
#ifdef BROOV_FRAME_SKIP_DEBUG
	//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "fps_lag : %lf", fps_lag);
#endif

	if (fps_lag <= 0 ){
		//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "No Skip needed, Faster than needed, taken care in refresh_timer");				 
		return 1;
	}else if (fps_lag >0 && fps_lag <6) {
		// Frame Rate on screen is 20 FPS 4:1
#ifdef BROOV_FRAME_SKIP_DEBUG
		//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "Lite Skip");
#endif
		if(skipafternumberofframessmall>0){
			skipafternumberofframessmall--;
			return 1;
		}else if(skipafternumberofframessmall<=0){
			skipafternumberofframessmall=4;
			return 0;
		}
	}else if (fps_lag >5 && fps_lag <10){
		// Frame Rate on screen is 16 FPS 2:1
#ifdef BROOV_FRAME_SKIP_DEBUG
		//	__android_log_print(ANDROID_LOG_INFO, "AV Skip", "medium Skip");
#endif
		if(skipafternumberofframes>0){
			skipafternumberofframes--;
			return 1;
		}else if(skipafternumberofframes<=0){
			skipafternumberofframes=2;
			return 0;
		}

	}
#if 0
	else {
		// Frame Rate on screen is 12 FPS 1:1
		if (alternateskip){
			alternateskip=false;
			return 1;
		}
		alternateskip=true;
		return 0;
	}
#else
	else if(fps_lag >=7&& fps_lag <10){
		// Frame Rate on screen is 12 FPS
#ifdef BROOV_FRAME_SKIP_DEBUG
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "above average Skip");
#endif
		if (alternateskip){
			alternateskip=false;
			return 1;
		}
		alternateskip=true;
		return 0;
	}else if(fps_lag >=10 && fps_lag <12){
		// Frame Rate on screen is 8 FPS
#ifdef BROOV_FRAME_SKIP_DEBUG
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "High Skip 1");
#endif
		if (skipnumberoftimes1>0){
			skipnumberoftimes1--;
			return 0;
		}else if(skipnumberoftimes1<=0){
			skipnumberoftimes1=2;
			return 1;
		}
	}else if(fps_lag >=12 && fps_lag <14){
		// Frame Rate on screen is 6 FPS
#ifdef BROOV_FRAME_SKIP_DEBUG
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "High Skip 2");
#endif
		if (skipnumberoftimes2>0){
			skipnumberoftimes2--;
			return 0;
		}else if(skipnumberoftimes2<=0){
			skipnumberoftimes2=3;
			return 1;
		}	
	}else if(fps_lag >=14&& fps_lag <16){
		// Frame Rate on screen is 4 FPS
#ifdef BROOV_FRAME_SKIP_DEBUG
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "Very High Skip 1");
#endif
		if (skipnumberoftimeslarge1>0){
			skipnumberoftimeslarge1--;
			return 0;
		}else if(skipnumberoftimeslarge1==0){
			skipnumberoftimeslarge1=5;
			return 1;
		}
	}else if(fps_lag >=16){
		// Frame Rate on screen is 2-3 FPS
#ifdef BROOV_FRAME_SKIP_DEBUG
		__android_log_print(ANDROID_LOG_INFO, "AV Skip", "Very High Skip 2");
#endif
		if (skipnumberoftimeslarge2>0){
			skipnumberoftimeslarge2--;
			return 0;
		}else if(skipnumberoftimeslarge2==0){
			skipnumberoftimeslarge2=7;
			return 1;
		}
	}
#endif
	return 1;
}
#endif /* BROOV_VIDEO_SKIP */

#ifdef BROOV_FRAME_RATE


FrameRateType fps_set[MAX_FPS_SETS][MAX_FPS_FRAMES];
FrameRateType fps_sum[MAX_FPS_SETS];
FrameRateType fps_avg[MAX_FPS_SETS];

int           fps_curr_offset[MAX_FPS_SETS];
int           fps_n[MAX_FPS_SETS];

void init_frame_rate(FrameRateType value, int n, int type)
{
	int i; 

	fps_sum[type] = 0;
	fps_n[type]   = n;

	for (i=0; i<n; i++) {

		fps_set[type][i] = value;

		fps_sum[type] += value;
	}
	fps_curr_offset[type]= 0;

	return;
} 

FrameRateType get_average_of_N(FrameRateType new_num, int type)
{
	FrameRateType old_num = fps_set[type][fps_curr_offset[type]];

	fps_sum[type] = fps_sum[type] + new_num - old_num;

	fps_avg[type] = fps_sum[type]/fps_n[type];

	fps_set[type][fps_curr_offset[type]] = new_num;

	fps_curr_offset[type]++; 

	if (fps_curr_offset[type] >= fps_n[type]) {
		fps_curr_offset[type] = 0;
	}

	return fps_avg[type];

}

int get_frames_to_skip()
{
	float my_file_fps = file_fps;
	float decode_fps  = fps_avg[BROOV_FRAME_DECODE_RATE];
	float display_fps = fps_avg[BROOV_FRAME_DISPLAY_RATE];
	float yuv2rgb_fps = fps_avg[BROOV_FRAME_YUV_2_RGB_RATE];

        double diff = get_master_clock(cur_stream) - cur_stream->video_clock;
	//__android_log_print(ANDROID_LOG_INFO, "AV Skip", "Decode:%2.2f YUV2RGB:%2.2f Display:%2.2f Delay:%f", decode_fps, yuv2rgb_fps, display_fps, diff);

        //if (diff < 0.15) return 1;
        //if (diff < MIN_TIME_TO_WAIT_FOR_SKIP) return 1;
	//return 0; 

	return 1; 
}

#endif /* #ifdef BROOV_FRAME_RATE */
