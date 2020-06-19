// TODO: implement save stat of players for resume in restart
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>
#include <vector>
#include <fstream>
#include <thread>
#include <boost/format.hpp>
#include <boost/filesystem/operations.hpp>
#include "utils.hpp"
#define BY_DVBLAST 1
using namespace std;
using nlohmann::json;
void start_channel(json tuner, live_setting live_config)
{
    BOOST_LOG_TRIVIAL(debug) << tuner.dump(4);
#if BY_DVBLAST
    string fromdvb_args = "";
    if (tuner["is_dvbt"])   
        fromdvb_args = "-f" + to_string(tuner["freq"]) + "000";
    else{
        int pol = tuner["pol"] == "H" ? 18 : 13;
        auto args = boost::format("-f%d000 -s%d000 -v%d -S%d") 
            % tuner["freq"].get<int>() 
            % tuner["symrate"].get<int>() 
            % pol 
            % tuner["switch"].get<int>();
        fromdvb_args = args.str();
    }
    string cfg_name = "/opt/sms/tmp/fromdvb_"+ to_string(tuner["_id"]);
    ofstream cfg(cfg_name);
    if(!cfg.is_open()) BOOST_LOG_TRIVIAL(error) << "Can't open fromdvb config file";
    for(auto& chan : tuner["channels"]){
        auto multicast = Util::get_multicast(live_config, chan["_id"]);

        int sid = chan["sid"].is_string() ? 
            std::stol(string(chan["sid"])) : chan["sid"].get<int>();
        auto addr = boost::format("%s:%d@127.0.0.1  1   %d\n") 
            % multicast % INPUT_PORT % sid;
        cfg << addr.str(); 
        BOOST_LOG_TRIVIAL(info) << "DVB:" << tuner["_id"] << " chan:" 
            << chan["name"]  << " -> " << multicast; 
    }
    cfg.close();
    auto cmd = boost::format("/opt/sms/bin/fromdvb -WYCUlu -t0 -a%d -c%s %s")
            % tuner["_id"] % cfg_name % fromdvb_args ; 
    Util::system(cmd.str());
#else
#endif
}
int main()
{
    Mongo db;
    vector<thread> pool;
    live_setting live_config;
    CHECK_LICENSE;
    Util::init(db);
    if(!Util::get_live_config(db, live_config, "dvb")){
        BOOST_LOG_TRIVIAL(error) << "Error in live config! Exit.";
        return -1;
    }
    json tuners = json::parse(db.find_mony("live_tuners_input", "{}"));
    if(tuners.is_null() || tuners.size() == 0){
        BOOST_LOG_TRIVIAL(warning) << "Live input tuners in empty!";
    }
    json silver_channels = json::parse(db.find_mony("live_output_silver", "{}"));
    for(auto& chan : silver_channels ){
        IS_CHANNEL_VALID(chan);
        if(chan["inputType"] == live_config.type_id){
            json dvb_chan = json::parse(db.find_id("live_inputs_dvb", chan["input"]));
            IS_CHANNEL_VALID(dvb_chan);
            for(auto& tuner : tuners ){
                int t_id = tuner["_id"];
                int c_id = dvb_chan["dvb_id"];
                if(t_id == c_id){
                    IS_CHANNEL_VALID(tuner);
                    if(tuner["channels"].is_null())
                        tuner["channels"] = json::array();
                    tuner["channels"].push_back(dvb_chan);
                    break;
                }
            }
        }
    }
    for(auto& tuner : tuners ){
        if(!tuner["channels"].is_null()){
            string dvb_path = "/dev/dvb/adapter" + to_string(tuner["_id"]); 
            if(boost::filesystem::exists(dvb_path)){
                pool.emplace_back(start_channel, tuner, live_config);
            }else{
                BOOST_LOG_TRIVIAL(error) << "DVB Not found: "<< dvb_path;
            }
        }
    }
    for(auto& t : pool){
        t.join();
    }
    THE_END;
} 
