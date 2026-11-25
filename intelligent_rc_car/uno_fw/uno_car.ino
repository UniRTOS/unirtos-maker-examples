// 基于 1_UNO.ino（20240326）修改，单字节命令协议：F/B/L/R/S/A/+/-
// 修改点：
//  1. 每次 loop 读空串口缓冲区，只保留最后一个有效命令，杂散字节直接丢弃，
//     避免缓冲区被杂波塞满导致真实命令被丢弃/延迟。
//  2. 去掉收到字节后的回显 Serial.println，避免回显->串扰->再回显的自激循环。
//  3. 距离打印限频为每 DIST_PRINT_MS 一次（设为 0 则关闭）。
//  4. pulseIn 加超时（默认 1s 会阻塞整个循环）。
//  5. 修正停车判断使用 serialData 而非 cmd 的问题。
//  6. 避障的"后退 500ms + 左转 500ms"改为非阻塞状态机，命令响应不再被 delay 卡住 1 秒。
//  7. 收到运动命令回 "CMD:x"（模组据此确认，未确认会重发）；收到无效字节打印 "RX?:hex" 便于排查线路干扰。
//  8. 前进与自动前进分离：'F' 直接前进不避障；新增 'A' 自动前进（超声波避障）。
//  9. 速度改为 0~10 档（遥控显示 0%~100%），'+'/'-' 每次加减 1 档（10%），默认 10 档。
//     PWM 映射：0 档 = 0（停）；1 档 = SPEED_PWM_MIN；10 档 = SPEED_PWM_MAX，中间等差。
//     电机低于约 150 的 PWM 带负载基本转不动，所以 1 档直接从 SPEED_PWM_MIN 起步。
//     速度档影响 F/B 及自动前进中的前进/后退；左右转保持原程序的固定 250。

// PWM 控制引脚
#define PWM1_PIN            5
#define PWM2_PIN            6
// 74HCT595N 芯片
#define SHCP_PIN            2
#define EN_PIN              7
#define DATA_PIN            8
#define STCP_PIN            4
// 超声波控制引脚
const int Trig = 12;
const int Echo = 13;

const int Forward       = 92;
const int Backward      = 163;
const int Turn_Left     = 172;
const int Turn_Right    = 83;
const int Stop          = 0;

const unsigned long ECHO_TIMEOUT_US = 40000UL;   // HC-SR04 无回波时高电平约 38ms
const unsigned long DIST_PRINT_MS   = 5000UL;

const int SPEED_LEVEL_MAX = 10;
const int SPEED_PWM_MIN   = 160;
const int SPEED_PWM_MAX   = 250;
const int TURN_PWM        = 250;

int speedLevel = SPEED_LEVEL_MAX;
int distance = 0;
char cmd = 'S';
unsigned long lastPrintMs = 0;

const unsigned long AVOID_STEP_MS = 500UL;
enum AvoidState { AV_RUN, AV_BACK, AV_TURN };
AvoidState avState = AV_RUN;
unsigned long avStepStart = 0;

void setup()
{
    Serial.begin(9600);

    pinMode(SHCP_PIN, OUTPUT);
    pinMode(EN_PIN, OUTPUT);
    pinMode(DATA_PIN, OUTPUT);
    pinMode(STCP_PIN, OUTPUT);
    pinMode(PWM1_PIN, OUTPUT);
    pinMode(PWM2_PIN, OUTPUT);
    pinMode(Trig, OUTPUT);
    pinMode(Echo, INPUT);

    Motor(Stop, 0);
}

void loop()
{
    distance = SR04(Trig, Echo);
    GetCommand();
}

int LevelPwm()
{
    if (speedLevel <= 0) return 0;
    return SPEED_PWM_MIN + (long)(speedLevel - 1) * (SPEED_PWM_MAX - SPEED_PWM_MIN) / (SPEED_LEVEL_MAX - 1);
}

void Motor(int Dir, int Speed)
{
    digitalWrite(EN_PIN, LOW);
    analogWrite(PWM1_PIN, Speed);
    analogWrite(PWM2_PIN, Speed);

    digitalWrite(STCP_PIN, LOW);
    shiftOut(DATA_PIN, SHCP_PIN, MSBFIRST, Dir);
    digitalWrite(STCP_PIN, HIGH);
}

int SR04(int Trig, int Echo)
{
    float cm = 0;
    digitalWrite(Trig, LOW);
    delayMicroseconds(2);
    digitalWrite(Trig, HIGH);
    delayMicroseconds(15);
    digitalWrite(Trig, LOW);

    cm = pulseIn(Echo, HIGH, ECHO_TIMEOUT_US) / 58.8;

    if (DIST_PRINT_MS > 0 && millis() - lastPrintMs >= DIST_PRINT_MS)
    {
        lastPrintMs = millis();
        Serial.print("Distance:");
        Serial.print(cm);
        Serial.println("cm");
    }
    delay(10);
    return cm;
}

void GetCommand()
{
    char newCmd = 0;
    int badPrinted = 0;

    while (Serial.available() > 0)
    {
        char c = Serial.read();
        if (c == 'F' || c == 'B' || c == 'L' || c == 'R' || c == 'S' || c == 'A')
        {
            newCmd = c;
        }
        else if (c == '+' || c == '-')
        {
            if (c == '+' && speedLevel < SPEED_LEVEL_MAX) speedLevel++;
            if (c == '-' && speedLevel > 0) speedLevel--;
            Serial.print("SPD:");
            Serial.println(speedLevel * 10);
        }
        else if (badPrinted < 4)
        {
            badPrinted++;
            Serial.print("RX?:");
            Serial.println((unsigned char)c, HEX);
        }
    }

    if (newCmd != 0)
    {
        if (newCmd != cmd) avState = AV_RUN;
        cmd = newCmd;
        Serial.print("CMD:");
        Serial.println(cmd);
    }

    if ('F' == cmd)
    {
        Motor(Forward, LevelPwm());
    }
    else if ('A' == cmd)
    {
        AvoidingObstacles();
    }
    else if ('B' == cmd)
    {
        Motor(Backward, LevelPwm());
    }
    else if ('L' == cmd)
    {
        Motor(Turn_Left, TURN_PWM);
    }
    else if ('R' == cmd)
    {
        Motor(Turn_Right, TURN_PWM);
    }
    else
    {
        Motor(Stop, 0);
    }
}

// 每次 loop 调用一次，不阻塞；distance 由 loop 开头的 SR04 更新。
void AvoidingObstacles()
{
    unsigned long now = millis();

    if (avState == AV_BACK)
    {
        if (now - avStepStart < AVOID_STEP_MS) return;
        Motor(Turn_Left, TURN_PWM);
        avState = AV_TURN;
        avStepStart = now;
        return;
    }
    if (avState == AV_TURN)
    {
        if (now - avStepStart < AVOID_STEP_MS) return;
        avState = AV_RUN;
    }

    if (distance > 20)
    {
        Motor(Forward, LevelPwm());
    }
    else
    {
        Motor(Backward, LevelPwm());
        avState = AV_BACK;
        avStepStart = now;
    }
}
