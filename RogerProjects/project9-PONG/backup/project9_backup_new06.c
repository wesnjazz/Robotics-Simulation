/*************************************************************************/
/* File:        project9.c                                               */
/* Description: User project #9 - PONG                                   */
/* Date:        12-2014                                                  */
/*************************************************************************/
#include <math.h>
#include <stdio.h>
#include "Xkw/Xkw.h"

#include "roger.h"
#include "simulate.h"
#include "control.h"
#include "modes.h"

// TODO:
// AIM - to attack forward not to my side
// RETREAT - if ball is in my side, then start chasing
// 					1. calculate angle from home position to current position
//					2. rotate towards opposite of home position
//					3. set x and y towards home so that Roger move backwards
//					4. Adjust heading to home[THETA] after arriving.
// BLOCK - never allow the ball to cross home position.
//
// "mode" variable - OFFENSE or DEFENSE
//						after PUNCH() is CONVERGED, then it is DEFFENSE mode.
//						after BLOCK() is CONVERGED, then it is OFFENSE mode.

/***
Offense()
	SearchTrack()
		Search()
		Track()
	ChaseGrabCenterAimPunch()
		ChaseGrab()
			Chase()
			Grab()
		CenterAimPunch()
			Center()
			Aim()
			Punch()
	Retreat()

Defense()
	SearchTrack()
		Search()
		Track()
	Block()
		Velocity()
		PredictPath()
		Intercept()

***/

#define RS_HOME 														0
#define RS_SEARCH 													1
#define RS_TRACK 														2
#define RS_SEARCH_TRACK 										3
#define RS_CHASE 														4
#define RS_PUNCH 														5
#define RS_CHASE_PUNCH 											6
#define RS_RETREAT 													7
#define RS_CHASE_PUNCH_RETREAT							8
// #define RS_GRAB															5
// #define RS_CENTER 													7
// #define RS_AIM 															8
// #define RS_CENTER_AIM_PUNCH 								10
// #define RS_CHASE_GRAB_CENTER_AIM_PUNCH			11
#define RS_OFFENSE													13
#define RS_VELOCITY 												14
#define RS_PREDICT 													15
#define RS_INTERCEPT 												16
#define RS_BLOCK														17
#define DEFENSE 														18
#define BALL_RND	-0.0
#define NBINS_BPS 50

typedef struct _setpoints {    /* base[THETA] added onto pre-existed struct */
  double base[3];             /* (x,y,theta) of the base in world frame */
  double arm[NARMS][NARM_JOINTS]; /* arm joint angles */
  double eye[NEYES];         /* eye verge angle relative to base frame */
} SetPoints;

typedef struct _ballpos {
	double x[NBINS_BPS];
	double y[NBINS_BPS];
	double time[NBINS_BPS];
	int head;
	int tail;
	int size;
	int cnt;
	double vel_x;
	double vel_y;
	int bps_created;
} BallPos;

SetPoints rec_setp[19];
Observation obs;
BallPos bps;
int bps_created = FALSE;
int state_search_track = NO_REFERENCE;

int init_BallPos(BallPos* bps) {
	for (int i=0; i<NBINS_BPS; i++) {
		bps->x[i] = BALL_RND;
		bps->y[i] = BALL_RND;
		bps->time[i] = BALL_RND;
	}
	bps->head = 0;
	bps->tail = 0;
	bps->size = NBINS_BPS;
	bps->cnt = 0;
	bps->vel_x = 0.0;
	bps->vel_y = 0.0;
}

int tt_search = 100;
int tt_search_interval = 200;
double prev_heading = 0.0;
int SEARCH(Robot* roger, double time)
/** eye[LEFT,RIGHT], base[THETA] **/
{
	tt_search++;
	double heading = 0.0;

	/** change eye angle every 100 and 200 ms from -M_PI to M_PI **/
	if (tt_search == 100) {
		rec_setp[RS_SEARCH].eye[LEFT] = -M_PI;
		rec_setp[RS_SEARCH].eye[RIGHT] = -M_PI;
	}
	if (tt_search == 200) {
		rec_setp[RS_SEARCH].eye[LEFT] = M_PI;
		rec_setp[RS_SEARCH].eye[RIGHT] = M_PI;
	}

	/** don't get new sample if the given interval is not fulfilled **/
	if (tt_search % tt_search_interval != 0) {
		return TRANSIENT;
	}

	tt_search = 0;

	/** if sampling succeeded **/
	if (sample_gaze_direction(&heading) == TRUE) {
		while (heading > M_PI || heading < 0.0) {	/** when heading is over M_PI, e.g., 3.1907 **/
			sample_gaze_direction(&heading);
		}
		while (fabs(prev_heading - heading) < M_PI/2.0) {
			sample_gaze_direction(&heading);
		}
		// printf("%3.3f- prev_heading:%3.3f  heading:%3.3f\n", time, prev_heading, heading);

		/** adjust gaze as eyes get closer to the heading **/
		double left_gaze = zeroTo2PI(heading) - zeroTo2PI(roger->base_position[THETA]);
		double right_gaze = left_gaze;

		/** set base_setpoint to the heading **/
		rec_setp[RS_SEARCH].base[THETA] = heading;
		prev_heading = heading;

		double cover_min = roger->base_position[THETA] - (M_PI/9.0);
		double cover_max = roger->base_position[THETA] + (M_PI/9.0);
		if (roger->base_setpoint[THETA] >= cover_min && roger->base_setpoint[THETA] <= cover_max) {
			/** if new setpoint is within range **/
			return CONVERGED;
		} else {
			/** if not within range **/
			return TRANSIENT;
		}
	} else {
		/** if sampling failed **/
		return NO_REFERENCE;
	}
}

int TRACK(Robot* roger, double time)
/** eye[LEFT,RIGHT], base[THETA] **/
{
	double ul = -1.0;
	double ur = -1.0;

	/** if a red pixel is detected on both eyes **/
	if ( average_red_pixel(roger, &ul, &ur) == TRUE ) {
		stereo_observation(roger, &obs);
		// printf("%3.3f- obs:(%3.3f,%3.3f)\n", time, obs->pos[X], obs->pos[Y]);
		/** get the eye angle error **/
		double eye_theta_error_l = atan2((ul - 63.5), FOCAL_LENGTH);
		double eye_theta_error_r = atan2((ur - 63.5), FOCAL_LENGTH);
		/** foveate **/
		rec_setp[RS_TRACK].eye[LEFT] = roger->eye_theta[LEFT] + eye_theta_error_l;
		rec_setp[RS_TRACK].eye[RIGHT] = roger->eye_theta[RIGHT] + eye_theta_error_r;
		rec_setp[RS_TRACK].base[THETA] = roger->base_position[THETA] + (roger->eye_theta[LEFT] + roger->eye_theta[RIGHT]) / 2.0;

		/** check if foveated **/
		double eye_diff = fabs(roger->eye_theta[LEFT] + roger->eye_theta[RIGHT]);
		double epsilon = 0.5;
		if (eye_diff < epsilon) {
			return CONVERGED;
		} else {
			/** if not foveated **/
			return TRANSIENT;
		}
	} else {
		/** if no red pixel is detected **/
		return NO_REFERENCE;
	}
}

int SEARCH_TRACK(Robot* roger, double time)
{
	/***********************************************************************************************/
	/* internal_state=[ 0:SEARCH 1:TRACK ] */
	/***********************************************************************************************/
	int internal_state[2];
	internal_state[0] = SEARCH(roger, time); // assigns values to rec_setp[0]
	internal_state[1] = TRACK(roger, time); // assigns values to rec_setp[1]
	int state = internal_state[1]*3 + internal_state[0];
	// printf("%3.3f, SEARCH:%d  TRACK:%d  Case:%d\n", time, internal_state[0], internal_state[1], state);
	int return_state;
	switch (state) {
						// TRACK 					SEARCH
		case 0: // NO_REFERENCE - NO_REFERENCE
		case 1: // NO_REFERENCE - TRANSIENT
		case 2: // NO_REFERENCE - CONVERGED
			rec_setp[RS_SEARCH_TRACK].eye[LEFT] = rec_setp[RS_SEARCH].eye[LEFT];
			rec_setp[RS_SEARCH_TRACK].eye[RIGHT] = rec_setp[RS_SEARCH].eye[RIGHT];
			rec_setp[RS_SEARCH_TRACK].base[THETA] = rec_setp[RS_SEARCH].base[THETA];
			rec_setp[RS_SEARCH_TRACK].base[X] = roger->base_position[X];
			rec_setp[RS_SEARCH_TRACK].base[Y] = roger->base_position[Y];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = NO_REFERENCE;
			state_search_track = NO_REFERENCE;
			break;
	
		case 3: // TRANSIENT - NO_REFERENCE
		case 4: // TRANSIENT - TRANSIENT
		case 5: // TRANSIENT - CONVERGED
			rec_setp[RS_SEARCH_TRACK].eye[LEFT] = rec_setp[RS_TRACK].eye[LEFT];
			rec_setp[RS_SEARCH_TRACK].eye[RIGHT] = rec_setp[RS_TRACK].eye[RIGHT];
			rec_setp[RS_SEARCH_TRACK].base[THETA] = rec_setp[RS_TRACK].base[THETA];
			rec_setp[RS_SEARCH_TRACK].base[X] = roger->base_position[X];
			rec_setp[RS_SEARCH_TRACK].base[Y] = roger->base_position[Y];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			// roger->eyes_setpoint[LEFT] = rec_setp[RS_TRACK].eye[LEFT];
			// roger->eyes_setpoint[RIGHT] = rec_setp[RS_TRACK].eye[RIGHT];
			// roger->base_setpoint[THETA] = rec_setp[RS_TRACK].base[THETA];
			return_state = TRANSIENT;
			state_search_track = TRANSIENT;
			break;
	
		case 6: // CONVERGED - NO_REFERENCE
		case 7: // CONVERGED - TRANSIENT
		case 8: // CONVERGED - CONVERGED
			rec_setp[RS_SEARCH_TRACK].eye[LEFT] = rec_setp[RS_TRACK].eye[LEFT];
			rec_setp[RS_SEARCH_TRACK].eye[RIGHT] = rec_setp[RS_TRACK].eye[RIGHT];
			rec_setp[RS_SEARCH_TRACK].base[THETA] = rec_setp[RS_TRACK].base[THETA];
			rec_setp[RS_SEARCH_TRACK].base[X] = roger->base_position[X];
			rec_setp[RS_SEARCH_TRACK].base[Y] = roger->base_position[Y];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_SEARCH_TRACK].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_SEARCH_TRACK].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = CONVERGED;
			state_search_track = CONVERGED;
			break;
		default:
		break;
	}
	return return_state;
}

int CHASE(Robot* roger, double time)
/** base[X,Y,THETA] **/
{
	// if (state_search_track == CONVERGED) {	/** if found the red ball **/

		/** set setpoints (x,y,theta) towards the red ball **/
		double angle = atan2(obs.pos[Y] - roger->base_position[Y], obs.pos[X] - roger->base_position[X]);
		rec_setp[RS_CHASE].base[X] = obs.pos[X];
		rec_setp[RS_CHASE].base[Y] = obs.pos[Y];
		rec_setp[RS_CHASE].base[THETA] = roger->base_position[THETA] + (roger->eye_theta[LEFT] + roger->eye_theta[RIGHT]) / 2.0;

		// rec_setp[RS_CHASE].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
		// rec_setp[RS_CHASE].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
		// rec_setp[RS_CHASE].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
		// rec_setp[RS_CHASE].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];

		/** determine if the base is reached the red ball within the range **/
		double X_err = obs.pos[X] - roger->base_position[X];
		double Y_err = obs.pos[Y] - roger->base_position[Y];
		double base_err = sqrt(X_err*X_err + Y_err*Y_err);
		double base_offset = 0.65;
		double epsilon = R_BALL + R_BASE + base_offset;
		if (base_err > epsilon) { return TRANSIENT; }
		else { 
			/** stop moving if reached to the red ball **/
			rec_setp[RS_CHASE].base[X] = roger->base_position[X];
			rec_setp[RS_CHASE].base[Y] = roger->base_position[Y];
			return CONVERGED; 
		}
	// }
	// rec_setp[RS_CHASE].base[X] = roger->base_position[X];
	// rec_setp[RS_CHASE].base[Y] = roger->base_position[Y];
	// rec_setp[RS_CHASE].base[THETA] = roger->base_position[THETA];
	return NO_REFERENCE;
}

int IsTactileRedBall(Robot* roger, double time, int limb)
{
	Observation obsBframe;
	double arm_x = 0.0, arm_y = 0.0;
	
	if (limb == LEFT) {
		fwd_arm_kinematics(roger->arm_theta[LEFT][0], roger->arm_theta[LEFT][1], &arm_x, &arm_y);
	} else if (limb == RIGHT) {
		fwd_arm_kinematics(roger->arm_theta[RIGHT][0], roger->arm_theta[RIGHT][1], &arm_x, &arm_y);
	}
	
	stereo_observation_Bframe(roger, &obsBframe);
	double d = sqrt( (obsBframe.pos[X] - arm_x)*(obsBframe.pos[X] - arm_x) + (obsBframe.pos[Y] - arm_y)*(obsBframe.pos[Y] - arm_y) );
	// printf("%.4f: R_BALL:%.4f  arm:(%.4f, %.4f)  ball:(%.4f, %.4f)  d=%.4f\n", time, R_BALL, arm_x, arm_y, obsBframe.pos[X], obsBframe.pos[Y], d);

	if (d < R_BALL + 0.6) { return TRUE; }
	else { return FALSE; }
}

int tiktok_punch = 1;
int tiktok_punch_nohit = 1;
int tiktok_punch_interval = 100;
int left_touched = FALSE;
int right_touched = FALSE;
int PUNCH(Robot* roger, double time)
/** base[X,Y,THETA], arms **/
{
	// if (state_search_track == CONVERGED) {
		int ball_LorR = LEFT;
		stereo_observation(roger, &obs);
		double angle = atan2(obs.pos[Y] - roger->base_position[Y], obs.pos[X] - roger->base_position[X]);
		double angle_err = roger->base_position[THETA] - angle;
		double angle_offset = 0.3;
		if (angle_err < 0) {ball_LorR = LEFT;}
		else {ball_LorR = RIGHT;}

		if (ball_LorR == LEFT) {
			// printf("\t\t\t\t\tBall is LEFT\n");
			rec_setp[RS_PUNCH].base[X] = obs.pos[X];
			rec_setp[RS_PUNCH].base[Y] = obs.pos[Y];
			rec_setp[RS_PUNCH].base[THETA] = roger->base_position[THETA] - M_PI;
			rec_setp[RS_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_PUNCH].arm[LEFT][1] = -1.2;
			rec_setp[RS_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
		} else if (ball_LorR == RIGHT) {
			// printf("\t\t\t\t\tBall is RIGHT\n");
			rec_setp[RS_PUNCH].base[X] = obs.pos[X];
			rec_setp[RS_PUNCH].base[Y] = obs.pos[Y];
			rec_setp[RS_PUNCH].base[THETA] = roger->base_position[THETA] + M_PI;
			rec_setp[RS_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_PUNCH].arm[RIGHT][1] = 1.2;
		} else {
			rec_setp[RS_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
		}

		double tolerance = 0.001;
		if (fabs(roger->ext_force[LEFT][1]) > tolerance && IsTactileRedBall(roger, time, LEFT) == TRUE) {
			printf("%.4f- Left Touched!\n", time);
			left_touched = TRUE;
		}
		if (fabs(roger->ext_force[RIGHT][1]) > tolerance && IsTactileRedBall(roger, time, RIGHT) == TRUE) {
			printf("%.4f- Right Touched!\n", time);
			right_touched = TRUE;
		}

		if (left_touched == TRUE || right_touched == TRUE) { 
			tiktok_punch++;
			if (tiktok_punch % tiktok_punch_interval == 0) {
				printf("STATE_CONVERGED()!!!\n");
				left_touched = right_touched = FALSE;
				tiktok_punch = 1;
				return CONVERGED; 
			}
		} else {
			tiktok_punch_nohit++;
			if (tiktok_punch_nohit % tiktok_punch_interval == 0 && left_touched == FALSE && right_touched == FALSE) {
				printf("No hit  STATE_RESET()\n");
				tiktok_punch_nohit = 1;
				return CONVERGED;
			}
		}


		return TRANSIENT;
	// }
	return NO_REFERENCE;
}

int CHASE_PUNCH(Robot* roger, double time)
{
	int internal_state[2];
	internal_state[0] = CHASE(roger, time);
	internal_state[1] = PUNCH(roger, time);
	int state = internal_state[0]*3 + internal_state[1];
	printf("%3.3f, CHASE:%d  PUNCH:%d  Case:%d\n", time, internal_state[0], internal_state[1], state);
	int return_state;
	switch (state) {
						// CHASE 					PUNCH
		case 0: // NO_REFERENCE - NO_REFERENCE
		case 1: // NO_REFERENCE - TRANSIENT
		case 2: // NO_REFERENCE - CONVERGED
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_SEARCH_TRACK].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_SEARCH_TRACK].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 3: // TRANSIENT - NO_REFERENCE
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_SEARCH_TRACK].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_SEARCH_TRACK].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 4: // TRANSIENT - TRANSIENT
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_CHASE].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_CHASE].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;

		case 5: // TRANSIENT - CONVERGED
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_CHASE].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_CHASE].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 6: // CONVERGED - NO_REFERENCE
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_CHASE].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_CHASE].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 7: // CONVERGED - TRANSIENT
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_PUNCH].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_PUNCH].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_PUNCH].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_PUNCH].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 8: // CONVERGED - CONVERGED
			rec_setp[RS_CHASE_PUNCH].eye[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH].eye[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH].base[THETA] = rec_setp[RS_PUNCH].base[THETA];
			rec_setp[RS_CHASE_PUNCH].base[X] = rec_setp[RS_PUNCH].base[X];
			rec_setp[RS_CHASE_PUNCH].base[Y] = rec_setp[RS_PUNCH].base[Y];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][0] = rec_setp[RS_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH].arm[LEFT][1] = rec_setp[RS_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0] = rec_setp[RS_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1] = rec_setp[RS_PUNCH].arm[RIGHT][1];
			return_state = CONVERGED;
			break;
		default:
			break;
	}
	return return_state;
}

int RETREAT(Robot* roger, double time)
/** base[X,Y,THETA], arms **/
{
	rec_setp[RS_RETREAT].arm[LEFT][0] = rec_setp[RS_HOME].arm[LEFT][0];
	rec_setp[RS_RETREAT].arm[LEFT][1] = rec_setp[RS_HOME].arm[LEFT][1];
	rec_setp[RS_RETREAT].arm[RIGHT][0] = rec_setp[RS_HOME].arm[RIGHT][0];
	rec_setp[RS_RETREAT].arm[RIGHT][1] = rec_setp[RS_HOME].arm[RIGHT][1];

	double X_err = rec_setp[RS_HOME].base[X] - roger->base_position[X];
	double Y_err = rec_setp[RS_HOME].base[Y] - roger->base_position[Y];

	double epsilon = 0.2;
	if (fabs(X_err) < epsilon && fabs(Y_err) < epsilon) {
		return CONVERGED;
	}

	double home_angle = atan2(Y_err, X_err);

	rec_setp[RS_RETREAT].base[X] = rec_setp[RS_HOME].base[X];
	rec_setp[RS_RETREAT].base[Y] = rec_setp[RS_HOME].base[Y];
	rec_setp[RS_RETREAT].base[THETA] = M_PI + home_angle;

	return TRANSIENT;
}

int CHASE_PUNCH_RETREAT(Robot* roger, double time)
{
	int internal_state[2];
	internal_state[0] = CHASE_PUNCH(roger, time);
	internal_state[1] = RETREAT(roger, time);
	int state = internal_state[0]*3 + internal_state[1];
	printf("%3.3f, CHASE_PUNCH:%d  RETREAT:%d  Case:%d\n", time, internal_state[0], internal_state[1], state);
	int return_state;
	switch (state) {
						// CHASE_PUNCH 		RETREAT
		case 0: // NO_REFERENCE - NO_REFERENCE
		case 1: // NO_REFERENCE - TRANSIENT
		case 2: // NO_REFERENCE - CONVERGED
		case 3: // TRANSIENT - NO_REFERENCE
		case 4: // TRANSIENT - TRANSIENT
		case 5: // TRANSIENT - CONVERGED
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT] = rec_setp[RS_CHASE_PUNCH].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT] = rec_setp[RS_CHASE_PUNCH].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA] = rec_setp[RS_CHASE_PUNCH].base[THETA];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[X] = rec_setp[RS_CHASE_PUNCH].base[X];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y] = rec_setp[RS_CHASE_PUNCH].base[Y];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;

		case 6: // CONVERGED - NO_REFERENCE
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT] = rec_setp[RS_CHASE_PUNCH].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT] = rec_setp[RS_CHASE_PUNCH].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA] = rec_setp[RS_CHASE_PUNCH].base[THETA];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[X] = rec_setp[RS_CHASE_PUNCH].base[X];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y] = rec_setp[RS_CHASE_PUNCH].base[Y];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 7: // CONVERGED - TRANSIENT
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT] = rec_setp[RS_CHASE_PUNCH].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT] = rec_setp[RS_CHASE_PUNCH].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA] = rec_setp[RS_RETREAT].base[THETA];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[X] = rec_setp[RS_RETREAT].base[X];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y] = rec_setp[RS_RETREAT].base[Y];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
		case 8: // CONVERGED - CONVERGED
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT] = rec_setp[RS_CHASE_PUNCH].eye[LEFT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT] = rec_setp[RS_CHASE_PUNCH].eye[RIGHT];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA] = rec_setp[RS_RETREAT].base[THETA];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[X] = rec_setp[RS_RETREAT].base[X];
			rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y] = rec_setp[RS_RETREAT].base[Y];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1] = rec_setp[RS_CHASE_PUNCH].arm[LEFT][1];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][0];
			rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1] = rec_setp[RS_CHASE_PUNCH].arm[RIGHT][1];
			return_state = CONVERGED;
			break;
		default:
			break;
	}
	return return_state;
}

int SEARCH_TRACK_CHASE_PUNCH_RETREAT(Robot* roger, double time)
{
	/***********************************************************************************************/
	/* internal_state=[ 0:SEARCH 1:TRACK ] */
	/***********************************************************************************************/
	int internal_state[2];
	internal_state[0] = SEARCH_TRACK(roger, time); // assigns values to rec_setp[0]
	internal_state[1] = CHASE_PUNCH_RETREAT(roger, time); // assigns values to rec_setp[1]
	int state = internal_state[0]*3 + internal_state[1];
	// printf("%3.3f, SEARCH_TRACK:%d  CHASE_PUNCH_RETREAT:%d  Case:%d\n", time, internal_state[0], internal_state[1], state);
	int return_state;
	switch (state) {
						// SEARCH_TRACK 	CHASE_PUNCH
		case 0: // NO_REFERENCE - NO_REFERENCE
		case 1: // NO_REFERENCE - TRANSIENT
		case 2: // NO_REFERENCE - CONVERGED
			roger->eyes_setpoint[LEFT] = rec_setp[RS_SEARCH_TRACK].eye[LEFT];
			roger->eyes_setpoint[RIGHT] = rec_setp[RS_SEARCH_TRACK].eye[RIGHT];
			roger->base_setpoint[THETA] = rec_setp[RS_SEARCH_TRACK].base[THETA];
			roger->base_setpoint[X] = rec_setp[RS_SEARCH_TRACK].base[X];
			roger->base_setpoint[Y] =	rec_setp[RS_SEARCH_TRACK].base[Y];
			roger->arm_setpoint[LEFT][0] = rec_setp[RS_SEARCH_TRACK].arm[LEFT][0];
			roger->arm_setpoint[LEFT][1] = rec_setp[RS_SEARCH_TRACK].arm[LEFT][1];
			roger->arm_setpoint[RIGHT][0] = rec_setp[RS_SEARCH_TRACK].arm[RIGHT][0];
			roger->arm_setpoint[RIGHT][1] = rec_setp[RS_SEARCH_TRACK].arm[RIGHT][1];
			return_state = NO_REFERENCE;
			break;
	
		case 3: // TRANSIENT - NO_REFERENCE
			roger->eyes_setpoint[LEFT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT];
			roger->eyes_setpoint[RIGHT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT];
			roger->base_setpoint[THETA] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA];
			roger->base_setpoint[X] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[X];
			roger->base_setpoint[Y] =	rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y];
			roger->arm_setpoint[LEFT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0];
			roger->arm_setpoint[LEFT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1];
			roger->arm_setpoint[RIGHT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0];
			roger->arm_setpoint[RIGHT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;

		case 4: // TRANSIENT - TRANSIENT
		case 5: // TRANSIENT - CONVERGED
			roger->eyes_setpoint[LEFT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT];
			roger->eyes_setpoint[RIGHT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT];
			roger->base_setpoint[THETA] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA];
			roger->base_setpoint[X] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[X];
			roger->base_setpoint[Y] =	rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y];
			roger->arm_setpoint[LEFT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0];
			roger->arm_setpoint[LEFT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1];
			roger->arm_setpoint[RIGHT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0];
			roger->arm_setpoint[RIGHT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1];
			return_state = TRANSIENT;
			break;
	
		case 6: // CONVERGED - NO_REFERENCE
			roger->eyes_setpoint[LEFT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT];
			roger->eyes_setpoint[RIGHT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT];
			roger->base_setpoint[THETA] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA];
			roger->base_setpoint[X] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[X];
			roger->base_setpoint[Y] =	rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y];
			roger->arm_setpoint[LEFT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0];
			roger->arm_setpoint[LEFT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1];
			roger->arm_setpoint[RIGHT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0];
			roger->arm_setpoint[RIGHT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1];
			return_state = TRANSIENT;
			return TRANSIENT;
		case 7: // CONVERGED - TRANSIENT
		case 8: // CONVERGED - CONVERGED
			roger->eyes_setpoint[LEFT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[LEFT];
			roger->eyes_setpoint[RIGHT] = rec_setp[RS_CHASE_PUNCH_RETREAT].eye[RIGHT];
			roger->base_setpoint[THETA] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[THETA];
			roger->base_setpoint[X] = rec_setp[RS_CHASE_PUNCH_RETREAT].base[X];
			roger->base_setpoint[Y] =	rec_setp[RS_CHASE_PUNCH_RETREAT].base[Y];
			roger->arm_setpoint[LEFT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][0];
			roger->arm_setpoint[LEFT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[LEFT][1];
			roger->arm_setpoint[RIGHT][0] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][0];
			roger->arm_setpoint[RIGHT][1] = rec_setp[RS_CHASE_PUNCH_RETREAT].arm[RIGHT][1];
			return_state = CONVERGED;
			break;
		default:
		break;
	}
	return return_state;
}

int PONG(Robot* roger, double time, SetPoints* rec_setp, Observation* obs, BallPos* bps)
{
	return TRANSIENT;
}

/************************************************************************/
void project9_control(roger, time)
Robot *roger;
double time;
{
	if (time < 0.0010) {
		printf("Recording Home position at time %.4f\n", time);
		rec_setp[RS_HOME].base[X] = roger->base_position[X];
		rec_setp[RS_HOME].base[Y] = roger->base_position[Y];
		rec_setp[RS_HOME].base[THETA] = roger->base_position[THETA];
		rec_setp[RS_HOME].arm[LEFT][0] = 2.0;
		rec_setp[RS_HOME].arm[LEFT][1] = -2.7;
		rec_setp[RS_HOME].arm[RIGHT][0] = -2.0;
		rec_setp[RS_HOME].arm[RIGHT][1] = 2.7;
		rec_setp[RS_HOME].eye[LEFT] = 0.0;
		rec_setp[RS_HOME].eye[RIGHT] = 0.0;
	}

	if (!bps_created) {
		init_BallPos(&bps);
		bps_created = TRUE;
	}

	SEARCH_TRACK_CHASE_PUNCH_RETREAT(roger, time);
}

/************************************************************************/
void project9_reset(roger)
Robot* roger;
{ }

// prompt for and read user customized input values
void project9_enter_params() 
{
	// Roger_mode = (Roger_mode == ROGER_MODE_DEFENSE) ? ROGER_MODE_OFFENSE : ROGER_MODE_DEFENSE;
	
}

//function called when the 'visualize' button on the gui is pressed
void project9_visualize(roger)
Robot* roger;
{ }

