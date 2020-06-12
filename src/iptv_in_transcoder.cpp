#include <boost/format/format_fwd.hpp>
#include <chrono>
#include <ctime>
#include <exception>
#include <iostream>
#include <vector>
#include <thread>
#include <boost/format.hpp>
#include "utils.hpp"
#define BY_FFMPEG 1
using namespace std;
using nlohmann::json;
void gst_task(string media_path, string multicast_addr, int port);
void start_channel(json channel, live_setting live_config)
{
    Mongo db;
    BOOST_LOG_TRIVIAL(info) << "Start Channel: " << channel["name"];
    if(!channel["active"]){
        BOOST_LOG_TRIVIAL(info) << channel["name"] << " is not Active. Exit!";
        return;
    }
    json profile = json::parse(db.find_id("live_transcode_profile",channel["profile"])); 
    if(profile["_id"].is_null()){
        BOOST_LOG_TRIVIAL(error) << "transcode profile id is in invalid:" 
                                 << channel["profile"];
        return;
    }
    auto out_multicast = Util::get_multicast(live_config, channel["_id"]);
    live_config.type_id = channel["inputType"];
    auto in_multicast  = Util::get_multicast(live_config, channel["input"]);
    // TODO: do by Gst
   /* 
                "_id": int,
                "active": boolean,
                "name": string, 
                "preset": string,       # from 'ultrafast','fast','medium','slow','veryslow' 
                "videoCodec": string,   # from  'h265','h264','mpeg2' 
                "videoSize": string,    # from '4K', 'FHD', 'HD', 'SD', 'CD' 
                "videoRate": int,       # from 1 .. 100000000 
                "videoFps": int,        # from 1 .. 60
                "videoProfile": string, # from 'Baseline', 'Main', 'High'
                "audioCodec": string,   # from 'aac','mp3','mp2' 
                "audioRate": int,       # from 1 to 1000000
                "extra": string 
    */
#if BY_FFMPEG 
    // TODO: apply  videoProfile and extra
    string vcodec = "copy" ,acodec = "copy", vsize = "720x576";
    string p_vcodec = profile["videoCodec"].get<string>();
    string p_acodec = profile["audioCodec"].get<string>();
    string p_vsize = profile["videoSize"].get<string>();
    if(p_vcodec.find("264") != string::npos)         vcodec = "libx264";
    else if(p_vcodec.find("mpeg2") != string::npos)  vcodec = "mpeg2video";
    else{
        BOOST_LOG_TRIVIAL(error) << "Vicedo Codec not support " << p_vcodec 
            << " for channel " << channel["name"];
        return;
    }
    if(p_acodec.find("mp3") != string::npos)         acodec = "libmp3lame";
    else if(p_acodec.find("mp2") != string::npos)    acodec = "mp2";
    else if(p_acodec.find("aac") != string::npos)    acodec = "aac";
    else{
        BOOST_LOG_TRIVIAL(error) << "Audio Codec not support " << p_acodec 
            << " for channel " << channel["name"];
        return;
    }
    if(p_vsize.find("SD") != string::npos)         vsize = "720x576";
    else if(p_vsize.find("FHD") != string::npos)  vsize = "1920x1080";
    else if(p_vsize.find("4K") != string::npos)   vsize = "4096x2048";
    else if(p_vsize.find("HD") != string::npos)   vsize = "1280x720";
    else if(p_vsize.find("CD") != string::npos)   vsize = "320x240";
    else{
        BOOST_LOG_TRIVIAL(error) << "Video size not support " << p_vsize 
            << " for channel " << channel["name"];
        return;
    }
    auto opts = boost::format(" -preset %s -s %s -r %s -g %s -b:v %s -b:a %s   ")
            % profile["preset"] % vsize % profile["videoFps"] 
            % profile["videoFps"] % profile["videoRate"] % profile["audioRate"];
    string extra = profile["extra"];
    auto codecs = boost::format("-vcodec %s -acodec %s %s") 
            % vcodec % acodec % extra ;
    auto cmd = boost::format("%s -i 'udp://%s:%d?reuse=1' "
            " %s %s -f mpegts 'udp://%s:%d?pkt_size=1316' ")
        % FFMPEG % in_multicast % INPUT_PORT % codecs.str() 
        % opts.str() % out_multicast % INPUT_PORT;
    
    Util::exec_shell_loop(cmd.str());
#else
    string in_uri = "udp://" + in_multicast + ":" + to_string(INPUT_PORT);
    gst_task(in_uri, multicast_out, INPUT_PORT);
#endif
    
}
int main()
{
    Mongo db;
    vector<thread> pool;
    live_setting live_config;
    CHECK_LICENSE;
    Util::init(db);
    if(!Util::get_live_config(db, live_config, "transcode")){
        BOOST_LOG_TRIVIAL(info) << "Error in live config! Exit.";
        return -1;
    }
    json silver_channels = json::parse(db.find_mony("live_output_silver", "{}"));
    for(auto& chan : silver_channels ){
        IS_CHANNEL_VALID(chan);
        if(chan["inputType"] == live_config.type_id){
            json transcode = json::parse(db.find_id("live_inputs_transcode", 
                        chan["input"]));
            IS_CHANNEL_VALID(transcode);
            pool.emplace_back(start_channel, transcode, live_config);
        }
    }
    for(auto& t : pool)
        t.join();
    THE_END;
} 
