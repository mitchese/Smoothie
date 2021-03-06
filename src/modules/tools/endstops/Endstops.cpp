/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libs/Module.h"
#include "libs/Kernel.h"
#include "modules/communication/utils/Gcode.h"
#include "modules/robot/Conveyor.h"
#include "Endstops.h"
#include "libs/nuts_bolts.h"
#include "libs/Pin.h"
#include "libs/StepperMotor.h"
#include "wait_api.h" // mbed.h lib
#include "Robot.h"
#include "Stepper.h"
#include "Config.h"
#include "SlowTicker.h"
#include "Planner.h"
#include "checksumm.h"
#include "utils.h"
#include "ConfigValue.h"

#define ALPHA_AXIS 0
#define BETA_AXIS  1
#define GAMMA_AXIS 2
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

#define NOT_HOMING 0
#define MOVING_TO_ORIGIN_FAST 1
#define MOVING_BACK 2
#define MOVING_TO_ORIGIN_SLOW 3

#define endstops_module_enable_checksum         CHECKSUM("endstops_enable")
#define corexy_homing_checksum                  CHECKSUM("corexy_homing")
#define delta_homing_checksum                   CHECKSUM("delta_homing")

#define alpha_min_endstop_checksum       CHECKSUM("alpha_min_endstop")
#define beta_min_endstop_checksum        CHECKSUM("beta_min_endstop")
#define gamma_min_endstop_checksum       CHECKSUM("gamma_min_endstop")

#define alpha_max_endstop_checksum       CHECKSUM("alpha_max_endstop")
#define beta_max_endstop_checksum        CHECKSUM("beta_max_endstop")
#define gamma_max_endstop_checksum       CHECKSUM("gamma_max_endstop")

#define alpha_trim_checksum              CHECKSUM("alpha_trim")
#define beta_trim_checksum               CHECKSUM("beta_trim")
#define gamma_trim_checksum              CHECKSUM("gamma_trim")

// these values are in steps and should be deprecated
#define alpha_fast_homing_rate_checksum  CHECKSUM("alpha_fast_homing_rate")
#define beta_fast_homing_rate_checksum   CHECKSUM("beta_fast_homing_rate")
#define gamma_fast_homing_rate_checksum  CHECKSUM("gamma_fast_homing_rate")

#define alpha_slow_homing_rate_checksum  CHECKSUM("alpha_slow_homing_rate")
#define beta_slow_homing_rate_checksum   CHECKSUM("beta_slow_homing_rate")
#define gamma_slow_homing_rate_checksum  CHECKSUM("gamma_slow_homing_rate")

#define alpha_homing_retract_checksum    CHECKSUM("alpha_homing_retract")
#define beta_homing_retract_checksum     CHECKSUM("beta_homing_retract")
#define gamma_homing_retract_checksum    CHECKSUM("gamma_homing_retract")
#define endstop_debounce_count_checksum  CHECKSUM("endstop_debounce_count")

// same as above but in user friendly mm/s and mm
#define alpha_fast_homing_rate_mm_checksum  CHECKSUM("alpha_fast_homing_rate_mm_s")
#define beta_fast_homing_rate_mm_checksum   CHECKSUM("beta_fast_homing_rate_mm_s")
#define gamma_fast_homing_rate_mm_checksum  CHECKSUM("gamma_fast_homing_rate_mm_s")

#define alpha_slow_homing_rate_mm_checksum  CHECKSUM("alpha_slow_homing_rate_mm_s")
#define beta_slow_homing_rate_mm_checksum   CHECKSUM("beta_slow_homing_rate_mm_s")
#define gamma_slow_homing_rate_mm_checksum  CHECKSUM("gamma_slow_homing_rate_mm_s")

#define alpha_homing_retract_mm_checksum    CHECKSUM("alpha_homing_retract_mm")
#define beta_homing_retract_mm_checksum     CHECKSUM("beta_homing_retract_mm")
#define gamma_homing_retract_mm_checksum    CHECKSUM("gamma_homing_retract_mm")

#define endstop_debounce_count_checksum  CHECKSUM("endstop_debounce_count")

#define alpha_homing_direction_checksum  CHECKSUM("alpha_homing_direction")
#define beta_homing_direction_checksum   CHECKSUM("beta_homing_direction")
#define gamma_homing_direction_checksum  CHECKSUM("gamma_homing_direction")
#define home_to_max_checksum             CHECKSUM("home_to_max")
#define home_to_min_checksum             CHECKSUM("home_to_min")
#define alpha_min_checksum               CHECKSUM("alpha_min")
#define beta_min_checksum                CHECKSUM("beta_min")
#define gamma_min_checksum               CHECKSUM("gamma_min")

#define alpha_max_checksum               CHECKSUM("alpha_max")
#define beta_max_checksum                CHECKSUM("beta_max")
#define gamma_max_checksum               CHECKSUM("gamma_max")

#define alpha_steps_per_mm_checksum      CHECKSUM("alpha_steps_per_mm")
#define beta_steps_per_mm_checksum       CHECKSUM("beta_steps_per_mm")
#define gamma_steps_per_mm_checksum      CHECKSUM("gamma_steps_per_mm")

Endstops::Endstops()
{
    this->status = NOT_HOMING;
    home_offset[0] = home_offset[1] = home_offset[2] = 0.0F;
}

void Endstops::on_module_loaded()
{
    // Do not do anything if not enabled
    if ( THEKERNEL->config->value( endstops_module_enable_checksum )->by_default(true)->as_bool() == false ) {
        return;
    }

    register_for_event(ON_CONFIG_RELOAD);
    this->register_for_event(ON_GCODE_RECEIVED);

    // Take StepperMotor objects from Robot and keep them here
    this->steppers[0] = THEKERNEL->robot->alpha_stepper_motor;
    this->steppers[1] = THEKERNEL->robot->beta_stepper_motor;
    this->steppers[2] = THEKERNEL->robot->gamma_stepper_motor;
    THEKERNEL->slow_ticker->attach( THEKERNEL->stepper->acceleration_ticks_per_second , this, &Endstops::acceleration_tick );

    // Settings
    this->on_config_reload(this);
}

// Get config
void Endstops::on_config_reload(void *argument)
{
    this->pins[0].from_string(         THEKERNEL->config->value(alpha_min_endstop_checksum          )->by_default("nc" )->as_string())->as_input();
    this->pins[1].from_string(         THEKERNEL->config->value(beta_min_endstop_checksum           )->by_default("nc" )->as_string())->as_input();
    this->pins[2].from_string(         THEKERNEL->config->value(gamma_min_endstop_checksum          )->by_default("nc" )->as_string())->as_input();
    this->pins[3].from_string(         THEKERNEL->config->value(alpha_max_endstop_checksum          )->by_default("nc" )->as_string())->as_input();
    this->pins[4].from_string(         THEKERNEL->config->value(beta_max_endstop_checksum           )->by_default("nc" )->as_string())->as_input();
    this->pins[5].from_string(         THEKERNEL->config->value(gamma_max_endstop_checksum          )->by_default("nc" )->as_string())->as_input();

    // we need to know steps per mm for M206, also use them for all settings
    this->steps_per_mm[0]           =  THEKERNEL->config->value(alpha_steps_per_mm_checksum         )->as_number();
    this->steps_per_mm[1]           =  THEKERNEL->config->value(beta_steps_per_mm_checksum          )->as_number();
    this->steps_per_mm[2]           =  THEKERNEL->config->value(gamma_steps_per_mm_checksum         )->as_number();

    //These are the old ones in steps still here for backwards compatibility
    this->fast_rates[0]             =  THEKERNEL->config->value(alpha_fast_homing_rate_checksum     )->by_default(4000 )->as_number();
    this->fast_rates[1]             =  THEKERNEL->config->value(beta_fast_homing_rate_checksum      )->by_default(4000 )->as_number();
    this->fast_rates[2]             =  THEKERNEL->config->value(gamma_fast_homing_rate_checksum     )->by_default(6400 )->as_number();
    this->slow_rates[0]             =  THEKERNEL->config->value(alpha_slow_homing_rate_checksum     )->by_default(2000 )->as_number();
    this->slow_rates[1]             =  THEKERNEL->config->value(beta_slow_homing_rate_checksum      )->by_default(2000 )->as_number();
    this->slow_rates[2]             =  THEKERNEL->config->value(gamma_slow_homing_rate_checksum     )->by_default(3200 )->as_number();
    this->retract_steps[0]          =  THEKERNEL->config->value(alpha_homing_retract_checksum       )->by_default(400  )->as_number();
    this->retract_steps[1]          =  THEKERNEL->config->value(beta_homing_retract_checksum        )->by_default(400  )->as_number();
    this->retract_steps[2]          =  THEKERNEL->config->value(gamma_homing_retract_checksum       )->by_default(1600 )->as_number();

    // newer mm based config values override the old ones, convert to steps/mm and steps, defaults to what was set in the older config settings above
    this->fast_rates[0] =    THEKERNEL->config->value(alpha_fast_homing_rate_mm_checksum )->by_default(this->fast_rates[0]  / steps_per_mm[0])->as_number() * steps_per_mm[0];
    this->fast_rates[1] =    THEKERNEL->config->value(beta_fast_homing_rate_mm_checksum  )->by_default(this->fast_rates[1]  / steps_per_mm[1])->as_number() * steps_per_mm[1];
    this->fast_rates[2] =    THEKERNEL->config->value(gamma_fast_homing_rate_mm_checksum )->by_default(this->fast_rates[2]  / steps_per_mm[2])->as_number() * steps_per_mm[2];
    this->slow_rates[0] =    THEKERNEL->config->value(alpha_slow_homing_rate_mm_checksum )->by_default(this->slow_rates[0]  / steps_per_mm[0])->as_number() * steps_per_mm[0];
    this->slow_rates[1] =    THEKERNEL->config->value(beta_slow_homing_rate_mm_checksum  )->by_default(this->slow_rates[1]  / steps_per_mm[1])->as_number() * steps_per_mm[1];
    this->slow_rates[2] =    THEKERNEL->config->value(gamma_slow_homing_rate_mm_checksum )->by_default(this->slow_rates[2]  / steps_per_mm[2])->as_number() * steps_per_mm[2];
    this->retract_steps[0] = THEKERNEL->config->value(alpha_homing_retract_mm_checksum   )->by_default(this->retract_steps[0] / steps_per_mm[0])->as_number() * steps_per_mm[0];
    this->retract_steps[1] = THEKERNEL->config->value(beta_homing_retract_mm_checksum    )->by_default(this->retract_steps[1] / steps_per_mm[1])->as_number() * steps_per_mm[1];
    this->retract_steps[2] = THEKERNEL->config->value(gamma_homing_retract_mm_checksum   )->by_default(this->retract_steps[2] / steps_per_mm[2])->as_number() * steps_per_mm[2];

    this->debounce_count  = THEKERNEL->config->value(endstop_debounce_count_checksum    )->by_default(0)->as_number();


    // get homing direction and convert to boolean where true is home to min, and false is home to max
    int home_dir                    = get_checksum(THEKERNEL->config->value(alpha_homing_direction_checksum)->by_default("home_to_min")->as_string());
    this->home_direction[0]         = home_dir != home_to_max_checksum;

    home_dir                        = get_checksum(THEKERNEL->config->value(beta_homing_direction_checksum)->by_default("home_to_min")->as_string());
    this->home_direction[1]         = home_dir != home_to_max_checksum;

    home_dir                        = get_checksum(THEKERNEL->config->value(gamma_homing_direction_checksum)->by_default("home_to_min")->as_string());
    this->home_direction[2]         = home_dir != home_to_max_checksum;

    this->homing_position[0]        =  this->home_direction[0] ? THEKERNEL->config->value(alpha_min_checksum)->by_default(0)->as_number() : THEKERNEL->config->value(alpha_max_checksum)->by_default(200)->as_number();
    this->homing_position[1]        =  this->home_direction[1] ? THEKERNEL->config->value(beta_min_checksum )->by_default(0)->as_number() : THEKERNEL->config->value(beta_max_checksum )->by_default(200)->as_number();;
    this->homing_position[2]        =  this->home_direction[2] ? THEKERNEL->config->value(gamma_min_checksum)->by_default(0)->as_number() : THEKERNEL->config->value(gamma_max_checksum)->by_default(200)->as_number();;

    this->is_corexy                 =  THEKERNEL->config->value(corexy_homing_checksum)->by_default(false)->as_bool();
    this->is_delta                  =  THEKERNEL->config->value(delta_homing_checksum)->by_default(false)->as_bool();

    // endstop trim used by deltas to do soft adjusting, in mm, convert to steps, and negate depending on homing direction
    // eg on a delta homing to max, a negative trim value will move the carriage down, and a positive will move it up
    int dirx = (this->home_direction[0] ? 1 : -1);
    int diry = (this->home_direction[1] ? 1 : -1);
    int dirz = (this->home_direction[2] ? 1 : -1);
    this->trim[0] = THEKERNEL->config->value(alpha_trim_checksum )->by_default(0  )->as_number() * steps_per_mm[0] * dirx;
    this->trim[1] = THEKERNEL->config->value(beta_trim_checksum  )->by_default(0  )->as_number() * steps_per_mm[1] * diry;
    this->trim[2] = THEKERNEL->config->value(gamma_trim_checksum )->by_default(0  )->as_number() * steps_per_mm[2] * dirz;
}

void Endstops::wait_for_homed(char axes_to_move)
{
    bool running = true;
    unsigned int debounce[3] = {0, 0, 0};
    while (running) {
        running = false;
        THEKERNEL->call_event(ON_IDLE);
        for ( char c = 'X'; c <= 'Z'; c++ ) {
            if ( ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
                if ( this->pins[c - 'X' + (this->home_direction[c - 'X'] ? 0 : 3)].get() ) {
                    if ( debounce[c - 'X'] < debounce_count ) {
                        debounce[c - 'X'] ++;
                        running = true;
                    } else if ( this->steppers[c - 'X']->moving ) {
                        this->steppers[c - 'X']->move(0, 0);
                    }
                } else {
                    // The endstop was not hit yet
                    running = true;
                    debounce[c - 'X'] = 0;
                }
            }
        }
    }
}

// this homing works for cartesian and delta printers, not for HBots/CoreXY
void Endstops::do_homing(char axes_to_move)
{
    // Start moving the axes to the origin
    this->status = MOVING_TO_ORIGIN_FAST;
    for ( char c = 'X'; c <= 'Z'; c++ ) {
        if ( ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
            this->feed_rate[c - 'X']= this->fast_rates[c - 'X'];
            this->steppers[c - 'X']->set_speed(0);
            this->steppers[c - 'X']->move(this->home_direction[c - 'X'], 10000000);
        }
    }

    // Wait for all axes to have homed
    this->wait_for_homed(axes_to_move);

    // Move back a small distance
    this->status = MOVING_BACK;
    bool inverted_dir;
    for ( char c = 'X'; c <= 'Z'; c++ ) {
        if ( ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
            inverted_dir = !this->home_direction[c - 'X'];
            this->feed_rate[c - 'X']= this->slow_rates[c - 'X'];
            this->steppers[c - 'X']->set_speed(0);
            this->steppers[c - 'X']->move(inverted_dir, this->retract_steps[c - 'X']);
        }
    }

    // Wait for moves to be done
    for ( char c = 'X'; c <= 'Z'; c++ ) {
        if (  ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
            while ( this->steppers[c - 'X']->moving ) {
                THEKERNEL->call_event(ON_IDLE);
            }
        }
    }

    // Start moving the axes to the origin slowly
    this->status = MOVING_TO_ORIGIN_SLOW;
    for ( char c = 'X'; c <= 'Z'; c++ ) {
        if ( ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
            this->feed_rate[c - 'X']= this->slow_rates[c - 'X'];
            this->steppers[c - 'X']->set_speed(0);
            this->steppers[c - 'X']->move(this->home_direction[c - 'X'], 10000000);
        }
    }

    // Wait for all axes to have homed
    this->wait_for_homed(axes_to_move);

    if (this->is_delta) {
        // move for soft trim
        this->status = MOVING_BACK;
        for ( char c = 'X'; c <= 'Z'; c++ ) {
            if ( this->trim[c - 'X'] != 0 && ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
                inverted_dir = !this->home_direction[c - 'X'];
                // move up or down depending on sign of trim
                if (this->trim[c - 'X'] < 0) inverted_dir = !inverted_dir;
                this->feed_rate[c - 'X']= this->slow_rates[c - 'X'];
                this->steppers[c - 'X']->set_speed(0);
                this->steppers[c - 'X']->move(inverted_dir, abs(this->trim[c - 'X']));
            }
        }

        // Wait for moves to be done
        for ( char c = 'X'; c <= 'Z'; c++ ) {
            if (  ( axes_to_move >> ( c - 'X' ) ) & 1 ) {
                //THEKERNEL->streams->printf("axis %c \r\n", c );
                while ( this->steppers[c - 'X']->moving ) {
                    THEKERNEL->call_event(ON_IDLE);
                }
            }
        }
    }

    // Homing is done
    this->status = NOT_HOMING;
}

void Endstops::wait_for_homed_corexy(int axis)
{
    bool running = true;
    unsigned int debounce[3] = {0, 0, 0};
    while (running) {
        running = false;
        THEKERNEL->call_event(ON_IDLE);
        if ( this->pins[axis + (this->home_direction[axis] ? 0 : 3)].get() ) {
            if ( debounce[axis] < debounce_count ) {
                debounce[axis] ++;
                running = true;
            } else {
                // turn both off if running
                if (this->steppers[X_AXIS]->moving) this->steppers[X_AXIS]->move(0, 0);
                if (this->steppers[Y_AXIS]->moving) this->steppers[Y_AXIS]->move(0, 0);
            }
        } else {
            // The endstop was not hit yet
            running = true;
            debounce[axis] = 0;
        }
    }
}

void Endstops::corexy_home(int home_axis, bool dirx, bool diry, float fast_rate, float slow_rate, unsigned int retract_steps)
{
    this->status = MOVING_TO_ORIGIN_FAST;
    this->feed_rate[X_AXIS]= fast_rate;
    this->steppers[X_AXIS]->set_speed(0);
    this->steppers[X_AXIS]->move(dirx, 10000000);
    this->feed_rate[Y_AXIS]= fast_rate;
    this->steppers[Y_AXIS]->set_speed(0);
    this->steppers[Y_AXIS]->move(diry, 10000000);

    // wait for primary axis
    this->wait_for_homed_corexy(home_axis);

    // Move back a small distance
    this->status = MOVING_BACK;
    this->feed_rate[X_AXIS]= slow_rate;
    this->steppers[X_AXIS]->set_speed(0);
    this->steppers[X_AXIS]->move(!dirx, retract_steps);
    this->feed_rate[Y_AXIS]= slow_rate;
    this->steppers[Y_AXIS]->set_speed(0);
    this->steppers[Y_AXIS]->move(!diry, retract_steps);

    // wait until done
    while ( this->steppers[X_AXIS]->moving || this->steppers[Y_AXIS]->moving) {
        THEKERNEL->call_event(ON_IDLE);
    }

    // Start moving the axes to the origin slowly
    this->status = MOVING_TO_ORIGIN_SLOW;
    this->feed_rate[X_AXIS]= slow_rate;
    this->steppers[X_AXIS]->set_speed(0);
    this->steppers[X_AXIS]->move(dirx, 10000000);
    this->feed_rate[Y_AXIS]= slow_rate;
    this->steppers[Y_AXIS]->set_speed(0);
    this->steppers[Y_AXIS]->move(diry, 10000000);

    // wait for primary axis
    this->wait_for_homed_corexy(home_axis);
}

// this homing works for HBots/CoreXY
void Endstops::do_homing_corexy(char axes_to_move)
{
    // TODO should really make order configurable, and select whether to allow XY to home at the same time, diagonally
    // To move XY at the same time only one motor needs to turn, determine which motor and which direction based on min or max directions
    // allow to move until an endstop triggers, then stop that motor. Speed up when moving diagonally to match X or Y speed
    // continue moving in the direction not yet triggered (which means two motors turning) until endstop hit

    if((axes_to_move & 0x03) == 0x03) { // both X and Y need Homing
        // determine which motor to turn and which way
        bool dirx= this->home_direction[X_AXIS];
        bool diry= this->home_direction[Y_AXIS];
        int motor;
        bool dir;
        if(dirx && diry) { // min/min
            motor= X_AXIS;
            dir= true;
        }else if(dirx && !diry) { // min/max
            motor= Y_AXIS;
            dir= true;
        }else if(!dirx && diry) { // max/min
            motor= Y_AXIS;
            dir= false;
        }else if(!dirx && !diry) { // max/max
            motor= X_AXIS;
            dir= false;
        }

        // then move both X and Y until one hits the endstop
        this->status = MOVING_TO_ORIGIN_FAST;
        this->feed_rate[motor]= this->fast_rates[motor]*1.4142;
        this->steppers[motor]->set_speed(0); // need to allow for more ground covered when moving diagonally
        this->steppers[motor]->move(dir, 10000000);
        // wait until either X or Y hits the endstop
        bool running= true;
        while (running) {
            THEKERNEL->call_event(ON_IDLE);
            for(int m=X_AXIS;m<=Y_AXIS;m++) {
                if(this->pins[m + (this->home_direction[m] ? 0 : 3)].get()) {
                    // turn off motor
                    if(this->steppers[motor]->moving) this->steppers[motor]->move(0, 0);
                    running= false;
                    break;
                }
            }
        }
    }

    // move individual axis
    if (axes_to_move & 0x01) { // Home X, which means both X and Y in same direction
        bool dir= this->home_direction[X_AXIS];
        corexy_home(X_AXIS, dir, dir, this->fast_rates[X_AXIS], this->slow_rates[X_AXIS], this->retract_steps[X_AXIS]);
    }

    if (axes_to_move & 0x02) { // Home Y, which means both X and Y in different directions
        bool dir= this->home_direction[Y_AXIS];
        corexy_home(Y_AXIS, dir, !dir, this->fast_rates[Y_AXIS], this->slow_rates[Y_AXIS], this->retract_steps[Y_AXIS]);
    }

    if (axes_to_move & 0x04) { // move Z
        do_homing(0x04); // just home normally for Z
    }

    // Homing is done
    this->status = NOT_HOMING;
}

// Start homing sequences by response to GCode commands
void Endstops::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);
    if ( gcode->has_g) {
        if ( gcode->g == 28 ) {
            gcode->mark_as_taken();
            // G28 is received, we have homing to do

            // First wait for the queue to be empty
            THEKERNEL->conveyor->wait_for_empty_queue();

            // Do we move select axes or all of them
            char axes_to_move = 0;
            // only enable homing if the endstop is defined, deltas always home all axis
            bool home_all = this->is_delta || !( gcode->has_letter('X') || gcode->has_letter('Y') || gcode->has_letter('Z') );

            for ( char c = 'X'; c <= 'Z'; c++ ) {
                if ( (home_all || gcode->has_letter(c)) && this->pins[c - 'X' + (this->home_direction[c - 'X'] ? 0 : 3)].connected() ) {
                    axes_to_move += ( 1 << (c - 'X' ) );
                }
            }

            // Enable the motors
            THEKERNEL->stepper->turn_enable_pins_on();

            // do the actual homing
            if (is_corexy)
                do_homing_corexy(axes_to_move);
            else
                do_homing(axes_to_move);

            // Zero the ax(i/e)s position, add in the home offset
            for ( int c = 0; c <= 2; c++ ) {
                if ( (axes_to_move >> c)  & 1 ) {
                    THEKERNEL->robot->reset_axis_position(this->homing_position[c] + this->home_offset[c], c);
                }
            }
        }
    } else if (gcode->has_m) {
        switch (gcode->m) {
            case 119: {

                int px = this->home_direction[0] ? 0 : 3;
                int py = this->home_direction[1] ? 1 : 4;
                int pz = this->home_direction[2] ? 2 : 5;
                const char *mx = this->home_direction[0] ? "min" : "max";
                const char *my = this->home_direction[1] ? "min" : "max";
                const char *mz = this->home_direction[2] ? "min" : "max";

                gcode->stream->printf("X %s:%d Y %s:%d Z %s:%d\n", mx, this->pins[px].get(), my, this->pins[py].get(), mz, this->pins[pz].get());
                gcode->mark_as_taken();
            }
            break;

            case 206: // M206 - set homing offset
                if (gcode->has_letter('X')) home_offset[0] = gcode->get_value('X');
                if (gcode->has_letter('Y')) home_offset[1] = gcode->get_value('Y');
                if (gcode->has_letter('Z')) home_offset[2] = gcode->get_value('Z');
                gcode->stream->printf("X %5.3f Y %5.3f Z %5.3f\n", home_offset[0], home_offset[1], home_offset[2]);
                gcode->mark_as_taken();
                break;

            case 500: // save settings
            case 503: // print settings
                gcode->stream->printf(";Home offset (mm):\nM206 X%1.2f Y%1.2f Z%1.2f\n", home_offset[0], home_offset[1], home_offset[2]);
                if (is_delta) {
                    float mm[3];
                    trim2mm(mm);
                    gcode->stream->printf(";Trim (mm):\nM666 X%1.2f Y%1.2f Z%1.2f\n", mm[0], mm[1], mm[2]);
                    gcode->stream->printf(";Max Z\nM665 Z%1.2f\n", this->homing_position[2]);
                }
                gcode->mark_as_taken();
                break;

            case 665: { // M665 - set max gamma/z height
                gcode->mark_as_taken();
                float gamma_max = this->homing_position[2];
                if (gcode->has_letter('Z')) {
                    this->homing_position[2] = gamma_max = gcode->get_value('Z');
                }
                gcode->stream->printf("Max Z %8.3f ", gamma_max);
                gcode->add_nl = true;
            }
            break;


            case 666: { // M666 - set trim for each axis in mm, NB negative mm and positive steps trim is down
                float mm[3];
                trim2mm(mm);

                if (gcode->has_letter('X')) mm[0] = gcode->get_value('X');
                if (gcode->has_letter('Y')) mm[1] = gcode->get_value('Y');
                if (gcode->has_letter('Z')) mm[2] = gcode->get_value('Z');

                int dirx = (this->home_direction[0] ? 1 : -1);
                int diry = (this->home_direction[1] ? 1 : -1);
                int dirz = (this->home_direction[2] ? 1 : -1);
                trim[0] = lround(mm[0] * steps_per_mm[0]) * dirx; // convert back to steps
                trim[1] = lround(mm[1] * steps_per_mm[1]) * diry;
                trim[2] = lround(mm[2] * steps_per_mm[2]) * dirz;

                // print the current trim values in mm and steps
                gcode->stream->printf("X %5.3f (%d) Y %5.3f (%d) Z %5.3f (%d)\n", mm[0], trim[0], mm[1], trim[1], mm[2], trim[2]);
                gcode->mark_as_taken();
            }
            break;

            // NOTE this is to test accuracy of lead screws etc.
            case 910: { // M910 - move specific number of raw steps
                int x= 0, y=0 , z= 0, f= 200*16;
                if (gcode->has_letter('F')) f = gcode->get_value('F');
                if (gcode->has_letter('X')) {
                    x = gcode->get_value('X');
                    this->steppers[X_AXIS]->set_speed(f);
                    this->steppers[X_AXIS]->move(x<0, abs(x));
                }
                if (gcode->has_letter('Y')) {
                    y = gcode->get_value('Y');
                    this->steppers[Y_AXIS]->set_speed(f);
                    this->steppers[Y_AXIS]->move(y<0, abs(y));
                }
                if (gcode->has_letter('Z')) {
                    z = gcode->get_value('Z');
                    this->steppers[Z_AXIS]->set_speed(f);
                    this->steppers[Z_AXIS]->move(z<0, abs(z));
                }
                gcode->stream->printf("Moved X %d Y %d Z %d F %d steps\n", x, y, z, f);
                gcode->mark_as_taken();
                break;
            }
        }
    }
}

void Endstops::trim2mm(float *mm)
{
    int dirx = (this->home_direction[0] ? 1 : -1);
    int diry = (this->home_direction[1] ? 1 : -1);
    int dirz = (this->home_direction[2] ? 1 : -1);

    mm[0] = this->trim[0] / this->steps_per_mm[0] * dirx; // convert to mm
    mm[1] = this->trim[1] / this->steps_per_mm[1] * diry;
    mm[2] = this->trim[2] / this->steps_per_mm[2] * dirz;
}

#define max(a,b) (((a) > (b)) ? (a) : (b))
// Called periodically to change the speed to match acceleration
uint32_t Endstops::acceleration_tick(uint32_t dummy)
{
    if(this->status == NOT_HOMING) return(0); // nothing to do

    // foreach stepper that is moving
    for ( int c = X_AXIS; c <= Z_AXIS; c++ ) {
        if( !this->steppers[c]->moving ) continue;

        uint32_t current_rate = this->steppers[c]->steps_per_second;
        uint32_t target_rate = int(floor(this->feed_rate[c]));

        if( current_rate < target_rate ){
            uint32_t rate_increase = int(floor((THEKERNEL->planner->acceleration/THEKERNEL->stepper->acceleration_ticks_per_second)*this->steps_per_mm[c]));
            current_rate = min( target_rate, current_rate + rate_increase );
        }
        if( current_rate > target_rate ){ current_rate = target_rate; }

        // steps per second
        this->steppers[c]->set_speed(max(current_rate, THEKERNEL->stepper->minimum_steps_per_second));
    }

    return 0;
}
