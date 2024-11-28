#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"

#include <elapsedMillis.h>
#include "ludevice.h"
//#include <esphome.h>


namespace esphome
{
  namespace mouse
  {
        static const char *const TAG = "mouse";
        double x = 0, y = 0, x0 = 0, y00 = 0, x1 = 0, y11 = 0, r = 100;
        elapsedMillis move_timer;
        int last_timer;
        ludevice kespb(2, 0);

        float mouseSpeed = 10.0f;
        float degreesToRadians = 2.0f * 3.14f / 360.0f;
        bool keydown = false;

        class Mouse : public switch_::Switch, public PollingComponent
        {
        public:
             Mouse() : PollingComponent(1000) {}
            bool enable = true;
            int max_random = 15000;
            
            bool pair()
            {
                int retcode;
                retcode = kespb.pair();
                if (retcode == true)
                {
                    if (kespb.register_device())
                    {
                        ESP_LOGD("INFO","Paired and connected");
                        return true;
                        //break;
                    }
                }
                else
                {
                    if (retcode == false)
                    {
                        ESP_LOGD("INFO","No dongle wants to pair");
                        //return false;
                        //delay(5000);
                        //break; 
                    }
                }
                return false;
            }
            void set_random(int rand) {
                if (rand < 1000) max_random = 1000;
                if (rand > 15000) max_random = 15000;
            }

            

            void write_state(bool state) override {
                // This will be called every time the user requests a state change.
                enable = state;
                // Acknowledge new state by publishing it
                publish_state(state);
            }

            void setup() override
            {
                ESP_LOGD("INFO", "start");
                kespb.begin();
                int i=0;
                publish_state(true);
                while (i<10)
                {
                    i++;
                    if (kespb.reconnect())
                    {
                        ESP_LOGD("INFO","Reconnected!");
                        break;
                    }
                    else
                    {
                        if (pair()) break;
                    }
                    yield();
                }
            }

            void left_rand() 
            {
                float i = 0;
                int delta = 0;
                for( i = 0; i < random(2,2)*PI; i = i + PI/random(2, 20)){
                    last_timer=move_timer;
                    r = i*random(20, 25) +i;
                    x1 = r * sin(i);
                    y11 = r * cos(i);
                    x = x1 - x0;
                    y = y11 - y00;
                    x0 = x1;
                    y00 = y11;
                    kespb.move(x,y);
                    delay(r/2);
                } 
            }

            void update() override
            {
                if (enable)
                {
                    if ((move_timer > random(1000, max_random))){
                            ESP_LOGD("INFO","Move mouse");        
                            left_rand();
                            move_timer=0;
                    }

                //   if (move_timer > 1000)
                //    {
                //        int x, y = 0;
                //        ESP_LOGD("INFO","moving mouse");
                //        kespb.typem(0xe9); // volume up
                //        kespb.typem(0xea); // volume down
                //        for (x = 0; x < 360; x += 5)
                //        { 
                //            kespb.move((uint16_t)(mouseSpeed * cos(((float)x) * degreesToRadians)),
                //                       (uint16_t)(mouseSpeed * sin(((float)x) * degreesToRadians)));
                            // delay(1000);
                            // kespb.typee();
                //        }
                //        move_timer = 0;
                //    }
                    kespb.loop();
                }
            }
        };
    }
}