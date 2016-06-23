#define VERSION 5

#define CTRL 512004

#define CTRL__MPU (1 << 0)
#define CTRL__LED(x) (((x) & 0xF) << 1)
#define CTRL__MOTOR_SEL(x) (((x) & 0xF) << 5)
#define CTRL__MOTOR_TEST(x) (((x) & 0xFFFF) << 9)

#define DEBUG 0

#define MOTOR 137429895

#define MOTOR__MIN(x) (((x) & 0xFFFF) << 0)
#define MOTOR__MAX(x) (((x) & 0xFFFF) << 16)

#define CMD_OFFSETS 67109205

#define CMD_OFFSETS__THROTTLE(x) (((x) & 0xFFFF) << 0)
#define CMD_OFFSETS__AIL_ELEV_RUD(x) (((x) & 0xFFFF) << 16)

#define THROTTLE_SCALE 1058416009
#define AILERON_SCALE 1044098796
#define ELEVATOR_SCALE 1044098796
#define RUDDER_SCALE 1044098796

#define PITCH_P 0
#define PITCH_I 0
#define PITCH_D 0

#define ROLL_P 0
#define ROLL_I 0
#define ROLL_D 0

#define YAW_P 0
#define YAW_I 0
#define YAW_D 0