// TODO: implement save stat of players for resume in restart
#include <chrono>
#include <ctime>
#include <exception>
#include <iostream>
#include <vector>
#include <thread>
#include <gstreamermm.h>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include "../third_party/json.hpp"
#include "mongo_driver.hpp"
#include "utils.hpp"
using namespace std;
using nlohmann::json;
void gst_task(string url, string multicast_addr, int port);

void start_channel(string channel_str, live_setting live_config)
{
    json channel = json::parse(channel_str);
    BOOST_LOG_TRIVIAL(info) << "Start Channel: " << channel["name"];
    if(!channel["active"]){
        BOOST_LOG_TRIVIAL(info) << channel["name"] << " is not Active. Exit!";
        return;
    }
    if(!channel["static"]){
        BOOST_LOG_TRIVIAL(info) << channel["name"] << " is not Static. Exit!";
        return;
    }
    auto multicast = get_multicast(live_config, channel["_id"]);
    gst_task(channel["url"], multicast, INPUT_PORT);
}
int main()
{
    vector<thread> pool;
    live_setting live_config;
    Gst::init();
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info);
    if(!get_live_config(live_config, "network")){
        BOOST_LOG_TRIVIAL(info) << "Error in live config! Exit.";
        return -1;
    }
    json silver_channels = json::parse(Mongo::find("live_output_silver", "{}"));
    for(auto& chan : silver_channels ){
        if(chan["active"] == true && chan["inputType"] == live_config.type_id){
            string network = Mongo::find_id("live_inputs_network", chan["inputId"]);
            pool.emplace_back(start_channel, network, live_config);
        }
    }
    for(auto& t : pool)
        t.join();
    return 0;
} 