/*
 * Copyright (c) 2020 Karim, karimdavoodi@gmail.com
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <exception>
#include <thread>
#include <map>
#include <glib.h>
#include <glib-object.h>
#include <gst/mpegts/mpegts.h>
#include "utils.hpp"
#include "gst.hpp"

using namespace std;

struct Event {
    string name;
    string text;
    int start = 0;
    int duration = 0;
};
struct DayTime {
    int hour = 0, minute = 0, second = 0;
};
struct Edata {
    Gst::Data d;
    map<int, Event> day_eit;
};

int gst_date_to_int(GstDateTime* date);
void gst_date_to_day_time(GstDateTime* date, DayTime& d);
string gst_date_to_str(GstDateTime* date);
void dump_tdt (GstMpegtsSection * section);
void dump_tot (GstMpegtsSection * section);
void dump_descriptors (GstMpegtsEITEvent *event, map<int, Event>& day_eit);
void dump_eit(GstMpegtsSection *sec, map<int, Event>& day_eit);
void channel_epg_update(Mongo& db, map<int, Event>& day_eit, int channel_id);
int bus_on_message(GstBus * bus, GstMessage * message, gpointer user_data);

/*
 *   The Gstreamer main function
 *   Capture EPG of udp:://in_multicast:port and update in DB
 *   
 *   @param in_multicast : multicast of input stream
 *   @param port: output multicast port numper 
 *   @param db: instanse of MongoDB
 *   @param channel_id: Id of channel in DB 
 *
 * */
void gst_get_epg_of_stream(Mongo& db, string in_multicast, int port, int channel_id)
{
    in_multicast = "udp://" + in_multicast + ":" + to_string(port);
    LOG(info) << "Start in " << in_multicast;

    Edata edata;
    edata.d.loop      = g_main_loop_new(nullptr, false);
    edata.d.pipeline  = GST_PIPELINE(gst_element_factory_make("pipeline", nullptr));

    try{
        auto udpsrc     = Gst::add_element(edata.d.pipeline, "udpsrc"),
             tsparse    = Gst::add_element(edata.d.pipeline, "tsparse"),
             fakesink   = Gst::add_element(edata.d.pipeline, "fakesink");

        gst_element_link_many(udpsrc, tsparse, fakesink, nullptr);

        g_object_set(udpsrc, "uri", in_multicast.c_str(), nullptr);
        edata.d.bus = gst_pipeline_get_bus(edata.d.pipeline);
        edata.d.watch_id = gst_bus_add_watch(edata.d.bus, bus_on_message, &edata); 
        bool thread_running = true;
        std::thread t([&](){
                while(thread_running){
                    int wait = EPG_UPDATE_TIME;
                    while(thread_running && wait--) Util::wait(1000);
                    try{
                    if(!edata.day_eit.empty()){
                        LOG(info) << "Try to save EPG in DB";
                        channel_epg_update(db, edata.day_eit, channel_id);
                    }else{
                        LOG(debug) << "The channel id " << channel_id << " dosn't have EPG";
                    }
                    }catch(std::exception& e){
                        LOG(error) << e.what();
                    }
                }
                });

        gst_element_set_state(GST_ELEMENT(edata.d.pipeline), GST_STATE_PLAYING);
        g_main_loop_run(edata.d.loop);
        thread_running = false;
        t.join();
    }catch(std::exception& e){
        LOG(error) << e.what();
    }
}
int gst_date_to_int(GstDateTime* date)
{
    int n;
    n  = 3600 * gst_date_time_get_hour(date);
    n += 60 * gst_date_time_get_minute(date);
    n += gst_date_time_get_second(date);
    return n;
}
void gst_date_to_day_time(GstDateTime* date, DayTime& d)
{
    d.hour = gst_date_time_get_hour(date);
    d.minute = gst_date_time_get_minute(date);
    d.second = gst_date_time_get_second(date);
}
string gst_date_to_str(GstDateTime* date)
{
    string ret;
    if(date){
        DayTime d;
        gst_date_to_day_time(date, d);
        ret = to_string(d.hour) + ":" + to_string(d.minute) 
                                + ":" + to_string(d.second);
    }
    return ret;
}
#if 0
DayTime current_time;
void dump_tdt (GstMpegtsSection * section)
{
    GstDateTime *date = gst_mpegts_section_get_tdt (section);
    if (date) {
        gst_date_to_day_time(date, current_time);
        gst_date_time_unref (date);
    }
}
void dump_tot (GstMpegtsSection * section)
{
    const GstMpegtsTOT *tot = gst_mpegts_section_get_tot (section);
    if (tot) {
        gst_date_to_day_time(tot->utc_time, current_time);
        //      gst_mpegts_section_unref(tot);
    }
}
*/
#endif
void dump_descriptors (GstMpegtsEITEvent *event, map<int, Event>& day_eit)
{
    GPtrArray *descriptors = event->descriptors;
    for (int i = 0; i < (int)descriptors->len; i++) {
        auto* desc = (GstMpegtsDescriptor *)
                                    g_ptr_array_index (descriptors, i);
        if(desc->tag == GST_MTS_DESC_DVB_SHORT_EVENT) {
            gchar *language_code, *event_name, *text;
            if (gst_mpegts_descriptor_parse_dvb_short_event (desc, &language_code,
                        &event_name, &text)) {
                Event e;
                e.start = gst_date_to_int(event->start_time);            
                e.duration = event->duration;
                e.name = event_name; 
                e.text = text;
                day_eit[e.start] = e;
                g_free (language_code);
                g_free (event_name);
                g_free (text);
            }
        }
    }
}
void dump_eit(GstMpegtsSection *sec, map<int, Event>& day_eit)
{
    const GstMpegtsEIT *eit = gst_mpegts_section_get_eit(sec);
    if(eit == nullptr){
        LOG(warning) << "Can't parse mpegts EIT";
        return;
    } 
    LOG(debug) << "EIT:"
        << " section_id " << sec->subtable_extension
        << " transport_stream_id " << eit->transport_stream_id
        << " original_network_id " << eit->original_network_id
        << " segment_last_section_number " << eit->segment_last_section_number
        << " last_table_id " << eit->last_table_id
        << " actual_stream " << (eit->actual_stream ? "true" : "false") 
        << " present_following " << (eit->present_following ? "TRUE" : "FALSE");
    int len = eit->events->len;
    LOG(debug) << "Event number:" << len;
    for (int i = 0; i < len; i++) {
        auto *event = (GstMpegtsEITEvent *)
            g_ptr_array_index(eit->events, i);
        if(event->running_status != 0) continue;
        LOG(debug) 
            <<  event->running_status
            << " event_id:" << event->event_id
            << " start_time:" << gst_date_to_str(event->start_time)
            << " duration:" << event->duration;
        dump_descriptors(event, day_eit);
    }
    //g_object_unref(GST_OBJECT(eit));
    
}
void channel_epg_update(Mongo& db, map<int, Event>& day_eit, int channel_id)
{
    json eit = json::array();
    for(auto & [start, event] : day_eit){
        json j;
        j["start"] = event.start;
        j["name"] = event.name;
        j["duration"] = event.duration;
        j["text"] = event.text;
        eit.push_back(j);
        LOG(debug) 
            <<  "Start:" << event.start
            <<  " Name:" << event.name
            <<  " Text:" << event.text
            <<  " Duration:" << event.duration;
    }
    
    day_eit.clear();
    json epg = json::object();
    epg["_id"] = channel_id;
    epg["total"] = eit.size();
    epg["content"] = eit;
    db.insert_or_replace_id("report_live_epg", channel_id, epg.dump());
    LOG(info) << "Update EPG of channel_id:" << channel_id;
}
int bus_on_message(GstBus * /*bus*/, GstMessage * message, gpointer user_data)
{
    auto edata = (Edata*) user_data;

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ELEMENT:
                GstMpegtsSection *sec;
                sec = gst_message_parse_mpegts_section(message);
                if(sec == nullptr){
                    LOG(warning) << "Can't parse mpegts section";
                    break;
                }
                if(sec->section_type == GST_MPEGTS_SECTION_EIT){
                    LOG(debug) << "Got EIT";
                    dump_eit(sec, edata->day_eit); 
                }
                gst_mpegts_section_unref (sec);
#if 0
                if(sec->section_type == GST_MPEGTS_SECTION_TOT)
                    dump_tot(sec); 
                if(sec->section_type == GST_MPEGTS_SECTION_TDT)
                    dump_tdt(sec); 
#endif
                break;
        case GST_MESSAGE_ERROR:
                {
                gchar *debug;
                GError *err;
                gst_message_parse_error (message, &err, &debug);
                LOG(warning) <<  err->message << " debug:" << debug;
                g_error_free (err);
                g_free (debug);
                g_main_loop_quit (edata->d.loop);
                break;
                }
        case GST_MESSAGE_EOS:
                LOG(error) <<  "Got EOS";    
                g_main_loop_quit(edata->d.loop);
                break;
        default: ;
    }
    return true;
}
