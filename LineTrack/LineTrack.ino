
/**
 * @file LineTrack.ino 电压检测和电机闭环控制
 * @author igcxl (igcxl@qq.com)
 * @brief 使用基于电阻分压原理的模块进行电压检测，可完成对锂电池电压大小的检测，在低电压时关闭电机。
 * @note 电压检测模块能使输入的电压缩小11倍。由于Arduino模拟输入电压最大为5V，故电压检测模块的输入电压不能大于5Vx11=55V。
 * Arduino的模拟量分辨率最小为5/1024=0.0049V，所以电压检测模块分辨率为0.0049Vx11=0.05371V。
 * 电压检测输入引脚A0
 * @version 0.3
 * @date 2019-09-09
 * @copyright Copyright © igcxl.com 2019
 * 
 */

#include <FlexiTimer2.h> ///<定时中断

//******************PWM引脚和电机驱动引脚******************//
const int AIN1 = 5;  ///<A路电机控制PWM波引脚1
const int AIN2 = 6;  ///<A路电机控制PWM波引脚2
const int BIN1 = 8;  ///<B路电机控制PWM波引脚1
const int BIN2 = 7;  ///<B路电机控制PWM波引脚2
const int CIN1 = 11; ///<C路电机控制PWM波引脚1
const int CIN2 = 12; ///<C路电机控制PWM波引脚2
const int DIN1 = 44; ///<D路电机控制PWM波引脚1
const int DIN2 = 46; ///<D路电机控制PWM波引脚2

//******************电机启动初始值**********************//
int motorDeadZone = 30;                 ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30,和电池电压、电机特性、PWM频率等有关，需要单独测试
int PWMConstrain = 255 - motorDeadZone; ///<电机PWM限幅值

//******************定时中断有关参数 **********************//
#define TIMER_PERIOD 10             ///<定时中断周期10ms
int Encoder_Pulses_Constrain = 75; ///<定时中断周期内最高转速对应编码器计数值，用于滤波,电机转速500rpm,每转输出390个脉冲，12V额定电压空载10ms二倍频的计数是500/60/1000*10*390*2=65个

//******************编码器引脚***************************//
#define ENCODER_A 2    ///<A路电机编码器引脚AA，外部中断，中断号0
#define ENCODER_B 3    ///<B路电机编码器引脚BA，外部中断，中断号1
#define ENCODER_C 18   ///<C路电机编码器引脚CA，外部中断，中断号5
#define ENCODER_D 19   ///<D路电机编码器引脚DA，外部中断，中断号4
#define DIRECTION_A 51 ///<A路电机编码器引脚AB
#define DIRECTION_B 53 ///<B路电机编码器引脚BB
#define DIRECTION_C 52 ///<C路电机编码器引脚CB
#define DIRECTION_D 50 ///<D路电机编码器引脚DB

//******************电压检测引脚***************************//
#define VOLTAGE A0 ///<电压检测引脚A0

//******************串口调试输出开关***************************//
#define DEBUG_INFO ///<调试输出开关，注释后关闭调试信息串口输出
//#define DEBUG_INFO1 ///<调试输出开关，注释后关闭调试信息串口输出

//******************循线传感器串口***************************//
#define LINE_TRACKER_SERIAL 		Serial2	//串口2
//**********************全局变量***********************//
volatile long g_Encoder_Pulses_A, g_Encoder_Pulses_B, g_Encoder_Pulses_C, g_Encoder_Pulses_D; ///<编码器脉冲数据
long Velocity_A, Velocity_B, Velocity_C, Velocity_D;                                          ///<各轮速度
float Velocity_KP = 3.0, Velocity_KI = 0.2;                                                   ///<PI参数
float Target_A, Target_B, Target_C, Target_D;                                                 ///<速度控制器倍率参数，目标参数
int Motora, Motorb, Motorc, Motord;                                                           ///<电机变量
int g_Battery_Voltage;                                                                        ///<电池电压采样变量,实际电压*100
/**
 * @brief 赋值给4路PWM寄存器
 * @note 重载函数，适用于4轮麦轮车
 * @param motora A路电机控制PWM
 * @param motorb B路电机控制PWM
 * @param motorc C路电机控制PWM
 * @param motord D路电机控制PWM
 */
void Set_PWM(int motora, int motorb, int motorc, int motord)
{
  if (motora > 0)
    analogWrite(AIN2, motora + motorDeadZone), analogWrite(AIN1, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motora == 0)
    analogWrite(AIN2, 0), analogWrite(AIN1, 0);
  else if (motora < 0)
    analogWrite(AIN1, -motora + motorDeadZone), analogWrite(AIN2, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motorb > 0)
    analogWrite(BIN2, motorb + motorDeadZone), analogWrite(BIN1, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorb == 0)
    analogWrite(BIN2, 0), analogWrite(BIN1, 0);
  else if (motorb < 0)
    analogWrite(BIN1, -motorb + motorDeadZone), analogWrite(BIN2, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motorc > 0)
    analogWrite(CIN1, motorc + motorDeadZone), analogWrite(CIN2, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorc == 0)
    analogWrite(CIN2, 0), analogWrite(CIN1, 0);
  else if (motorc < 0)
    analogWrite(CIN2, -motorc + motorDeadZone), analogWrite(CIN1, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motord > 0)
    analogWrite(DIN1, motord + motorDeadZone), analogWrite(DIN2, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motord == 0)
    analogWrite(DIN1, 0), analogWrite(DIN2, 0);
  else if (motord < 0)
    analogWrite(DIN2, -motord + motorDeadZone), analogWrite(DIN1, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30
}

/**
 * @brief 赋值给2路PWM寄存器
 * @note 重载函数，适用于2或4定向轮车
 * @param motorLeft  左路电机控制PWM-->  motorb 和 motora
 * @param motorRight 右路电机控制PWM-->  motorc 和 motord
 */
void Set_PWM(int motorLeft, int motorRight)
{
  if (motorLeft > 0)
    analogWrite(AIN2, motorLeft + motorDeadZone), analogWrite(AIN1, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorLeft == 0)
    analogWrite(AIN2, 0), analogWrite(AIN1, 0);
  else if (motorLeft < 0)
    analogWrite(AIN1, -motorLeft + motorDeadZone), analogWrite(AIN2, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motorLeft > 0)
    analogWrite(BIN2, motorLeft + motorDeadZone), analogWrite(BIN1, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorLeft == 0)
    analogWrite(BIN2, 0), analogWrite(BIN1, 0);
  else if (motorLeft < 0)
    analogWrite(BIN1, -motorLeft + motorDeadZone), analogWrite(BIN2, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motorRight > 0)
    analogWrite(CIN1, motorRight + motorDeadZone), analogWrite(CIN2, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorRight == 0)
    analogWrite(CIN2, 0), analogWrite(CIN1, 0);
  else if (motorRight < 0)
    analogWrite(CIN2, -motorRight + motorDeadZone), analogWrite(CIN1, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30

  if (motorRight > 0)
    analogWrite(DIN1, motorRight + motorDeadZone), analogWrite(DIN2, 0); ///<赋值给PWM寄存器根据电机响应速度与机械误差微调,
  else if (motorRight == 0)
    analogWrite(DIN1, 0), analogWrite(DIN2, 0);
  else if (motorRight < 0)
    analogWrite(DIN2, -motorRight + motorDeadZone), analogWrite(DIN1, 0); ///<高频时电机启动初始值高约为130，低频时电机启动初始值低约为30
}

/**
 * @brief 增量PI控制器
 * @param Target 目标速度
 * @param Encoder 编码器测量值
 * @return int 电机PWM
 * @note 根据增量式离散PID公式
 *pwm+=Kp[e（k）-e(k-1)]+Ki*e(k)+Kd[e(k)-2e(k-1)+e(k-2)]，
 *e(k)代表本次偏差，
 *e(k-1)代表上一次的偏差  以此类推，
 *pwm代表增量输出，
 *在我们的速度控制闭环系统里面，只使用PI控制，
 *pwm+=Kp[e（k）-e(k-1)]+Ki*e(k)。
 */
int Incremental_PI_A(int Target, int Encoder)
{
  float Error;
  static float PWM, LastError;
  Error = Target - Encoder;                                       ///<计算偏差 Error = 目标值 - 实测值
  PWM += Velocity_KP * (Error - LastError) + Velocity_KI * Error; ///<增量式PI控制器
  PWM = constrain(PWM, -PWMConstrain, PWMConstrain);              ///<限幅
  LastError = Error;                                              ///<保存上一次偏差
  return PWM;                                                     ///<增量输出
}
int Incremental_PI_B(int Target, int Encoder)
{
  float Error;
  static float PWM, LastError;
  Error = Target - Encoder;                                       //计算偏差
  PWM += Velocity_KP * (Error - LastError) + Velocity_KI * Error; //增量式PI控制器
  PWM = constrain(PWM, -PWMConstrain, PWMConstrain);              //限幅
  LastError = Error;                                              //保存上一次偏差
  return PWM;                                                     //增量输出
}
/**************************************************************************/
int Incremental_PI_C(int Target, int Encoder)
{
  float Error;
  static float PWM, LastError;
  Error = Target - Encoder;                                       //计算偏差
  PWM += Velocity_KP * (Error - LastError) + Velocity_KI * Error; //增量式PI控制器
  PWM = constrain(PWM, -PWMConstrain, PWMConstrain);              //限幅
  LastError = Error;                                              //保存上一次偏差
  return PWM;                                                     //增量输出
}
/**************************************************************************/
int Incremental_PI_D(int Target, int Encoder)
{
  float Error;
  static float PWM, LastError;
  Error = Target - Encoder;                                       //计算偏差
  PWM += Velocity_KP * (Error - LastError) + Velocity_KI * Error; //增量式PI控制器
  PWM = constrain(PWM, -PWMConstrain, PWMConstrain);              //限幅
  LastError = Error;                                              //保存上一次偏差
  return PWM;                                                     //增量输出
}
/**************************************************************************



/**
 * @brief 外部中断读取编码器数据
 * @note 注意外部中断是跳变沿触发，具有二倍频功能。
 */
void READ_ENCODER_A()
{
  if (digitalRead(ENCODER_A) == LOW)
  { //如果是下降沿触发的中断
    if (digitalRead(DIRECTION_A) == LOW)
      g_Encoder_Pulses_A--; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_A++;
  }
  else
  { //如果是上升沿触发的中断
    if (digitalRead(DIRECTION_A) == LOW)
      g_Encoder_Pulses_A++; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_A--;
  }
}


/**
 * @brief 外部中断读取编码器数据
 * @note 注意外部中断是跳变沿触发，具有二倍频功能。
 */
void READ_ENCODER_B()
{
  if (digitalRead(ENCODER_B) == LOW)
  { ///如果是下降沿触发的中断
    if (digitalRead(DIRECTION_B) == LOW)
      g_Encoder_Pulses_B++; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_B--;
  }
  else
  { //如果是上升沿触发的中断
    if (digitalRead(DIRECTION_B) == LOW)
      g_Encoder_Pulses_B--; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_B++;
  }
}

/**
 * @brief 外部中断读取编码器数据
 * @note 注意外部中断是跳变沿触发，具有二倍频功能。
 */
void READ_ENCODER_C()
{
  if (digitalRead(ENCODER_C) == LOW)
  { //如果是下降沿触发的中断
    if (digitalRead(DIRECTION_C) == LOW)
      g_Encoder_Pulses_C++; //根据另外一相电平判定方向
    else
      g_Encoder_Pulses_C--;
  }
  else
  { //如果是上升沿触发的中断
    if (digitalRead(DIRECTION_C) == LOW)
      g_Encoder_Pulses_C--; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_C++;
  }
}

/**
 * @brief 外部中断读取编码器数据
 * @note 注意外部中断是跳变沿触发，具有二倍频功能。
 */
void READ_ENCODER_D()
{
  if (digitalRead(ENCODER_D) == LOW)
  { ///如果是下降沿触发的中断
    if (digitalRead(DIRECTION_D) == LOW)
      g_Encoder_Pulses_D++; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_D--;
  }
  else
  { ///如果是上升沿触发的中断
    if (digitalRead(DIRECTION_D) == LOW)
      g_Encoder_Pulses_D--; ///<根据另外一相电平判定方向
    else
      g_Encoder_Pulses_D++;
  }
}


/**
 * @brief 异常关闭电机
 * 
 * @return unsigned char 返回断电状态 1=断电 0=通电 值：1：异常  0：正常
 */
unsigned char Turn_Off()
{
  byte temp;
  if (g_Battery_Voltage < 1100)
  { 
    temp = 1;
    digitalWrite(AIN1, LOW); ///<电机A驱动的电平控制
    digitalWrite(AIN2, LOW); ///<电机A驱动的电平控制
    digitalWrite(BIN1, LOW); //电机B驱动的电平控制
    digitalWrite(BIN2, LOW); //电机B驱动的电平控制
    digitalWrite(CIN1, LOW); //电机C驱动的电平控制
    digitalWrite(CIN2, LOW); //电机C驱动的电平控制
    digitalWrite(DIN1, LOW); //电机D驱动的电平控制
    digitalWrite(DIN2, LOW); //电机D驱动的电平控制
  }
  else
    temp = 0;
  return temp;
}

/**
 * @brief 定时中断执行函数
 * @note 核心函数，采集电压，获取速度，速度PI控制
 * 
 */
void Control()
{
  int voltageTemp;             ///<电压采样临时变量
  static float s_Voltage_Sum;  ///<电压采样周期累加值
  static byte s_Voltage_Count; ///<电压采样周期
  sei();                       ///<全局中断开启

  ///获取电机A速度
  if (abs(g_Encoder_Pulses_A) < Encoder_Pulses_Constrain)
  {
    Velocity_A = -g_Encoder_Pulses_A;
    g_Encoder_Pulses_A = 0; //读取编码器数据并根据实际接线做调整、然后清零，这就是通过M法测速（单位时间内的脉冲数）得到速度。
  }
  else
  {
    g_Encoder_Pulses_A = 0; //超出范围的延用上次的速度值、然后清零。
  }
  ///获取电机B速度
  if (abs(g_Encoder_Pulses_B) < Encoder_Pulses_Constrain)
  {
    Velocity_B = g_Encoder_Pulses_B;
    g_Encoder_Pulses_B = 0; ///<读取编码器数据并根据实际接线做调整、然后清零，这就是通过M法测速（单位时间内的脉冲数）得到速度。
  }
  else
  {
    g_Encoder_Pulses_B = 0; //超出范围的延用上次的速度值、然后清零。
  }
  ///获取电机C速度
  if (abs(g_Encoder_Pulses_C) < Encoder_Pulses_Constrain)
  {
    Velocity_C = -g_Encoder_Pulses_C;
    g_Encoder_Pulses_C = 0; //读取编码器数据并根据实际接线做调整、然后清零，这就是通过M法测速（单位时间内的脉冲数）得到速度。
  }
  else
  {
    g_Encoder_Pulses_C = 0; //超出范围的延用上次的速度值、然后清零。
  }
  ///获取电机D速度
  if (abs(g_Encoder_Pulses_D) < Encoder_Pulses_Constrain)
  {
    Velocity_D = -g_Encoder_Pulses_D;
    g_Encoder_Pulses_D = 0; ///<读取编码器数据并根据实际接线做调整、然后清零，这就是通过M法测速（单位时间内的脉冲数）得到速度。
  }
  else
  {
    g_Encoder_Pulses_D = 0; ///<超出范围的延用上次的速度值、然后清零。
  }

  Motora = Incremental_PI_A(Target_A, Velocity_A); ///<速度PI控制器
  Motorb = Incremental_PI_B(Target_B, Velocity_B); ///<速度PI控制器
  Motorc = Incremental_PI_C(Target_C, Velocity_C); ///<速度PI控制器
  Motord = Incremental_PI_D(Target_D, Velocity_D); ///<速度PI控制器

  if (Turn_Off() == 0)
  {
    Set_PWM(Motora, Motorb, Motorc, Motord); //如果电压不存在异常，使能电机
  }

  voltageTemp = analogRead(VOLTAGE); ///<采集电池电压
  s_Voltage_Count++;                 ///<平均值计数器
  s_Voltage_Sum += voltageTemp;      ///<多次采样累积
  if (s_Voltage_Count == 100)
  {
    g_Battery_Voltage = s_Voltage_Sum * 0.05371; ///<求电压平均值
    s_Voltage_Sum = 0;
    s_Voltage_Count = 0;
  }
}

/**
 * @brief 读取数据
 * 
 * @param Data 读取到的数据指针
 */
void Read_Data(unsigned int *Data);
{	
  unsigned char y = 0;
  unsigned char UART_RX_STA[3] = { 0 };       //数据缓存区
  unsigned char Num = 0;              //数组计数
  unsigned int Receive_data = 0;       //数据缓存区
	
  LINE_TRACKER_SERIAL.write(0x57);
 ///////////////////////////数字量数值///////////////////////////////	
 
  for(y=0;y <= 5;y++)
  {
    delay(1);
    if(LINE_TRACKER_SERIAL.available() > 0)
    {
      UART_RX_STA[Num++] = LINE_TRACKER_SERIAL.read();	//依次读取接收到的数据
      if(Num == 1)
      {
        Num = 0;
        *Data = UART_RX_STA[0];
        break;
      }
    } 
  }
  
///////////////////////////数字量数值///////////////////////////////	

///////////////////////////偏移量数值///////////////////////////////

//  for(y=0;y <= 10;y++)
//  {
//    delay(1);
//    if(WL_SERIAL.available() > 0)
//    {
//      USART_RX_STA[Num++] = WL_SERIAL.read();	//依次读取接收到的数据
//      if(Num == 3)
//      {
//        Num = 0;
//        
//        Receive_data = USART_RX_STA[1];
//        Receive_data <<= 8;
//        Receive_data |= USART_RX_STA[2];
//        
//        *Data = USART_RX_STA[0];
//        *(Data+1) = Receive_data;
//        
//        break;
//      }
//    } 
//  }
  
///////////////////////////偏移量数值///////////////////////////////	
	
}

/**
 * @brief 初始化函数
 * 
 */
void setup()
{

  //  int fff = 1;
  //  TCCR1B =(TCCR1B & 0xF8) | fff;//调整计数器分频，频率调高至31.374KHZ
  //  TCCR3B =(TCCR3B & 0xF8) | fff;//调整计数器分频，频率调高至31.374KHZ
  //  TCCR4B =(TCCR4B & 0xF8) | fff;//调整计数器分频，频率调高至31.374KHZ
  //  TCCR5B =(TCCR5B & 0xF8) | fff;//调整计数器分频，频率调高至31.374KHZ
  pinMode(AIN1, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(CIN1, OUTPUT);
  pinMode(DIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(CIN2, OUTPUT);
  pinMode(DIN2, OUTPUT);
  pinMode(ENCODER_A, INPUT);
  pinMode(ENCODER_B, INPUT);
  pinMode(ENCODER_C, INPUT);
  pinMode(ENCODER_D, INPUT);
  pinMode(DIRECTION_A, INPUT);
  pinMode(DIRECTION_B, INPUT);
  pinMode(DIRECTION_C, INPUT);
  pinMode(DIRECTION_D, INPUT);
  Serial.begin(9600);///<调试用串口
  LINE_TRACKER_SERIAL.begin(9600);///<循线传感器串口
  delay(300);                                 ///<延时等待初始化完成
  FlexiTimer2::set(TIMER_PERIOD, Control);    ///<10毫秒定时中断函数
  FlexiTimer2::start();                       ///<中断使能
  attachInterrupt(0, READ_ENCODER_A, CHANGE); ///<开启外部中断 编码器接口A
  attachInterrupt(1, READ_ENCODER_B, CHANGE); ///<开启外部中断 编码器接口B
  attachInterrupt(5, READ_ENCODER_C, CHANGE); ///<开启外部中断 编码器接口C
  attachInterrupt(4, READ_ENCODER_D, CHANGE); ///<开启外部中断 编码器接口D
  delay(100);
}


/**
 * @brief 主函数
 * 
 */
void loop()
{

  ///电机转速测试
  for (int i = 0; i < Encoder_Pulses_Constrain; i++)
  {
    Target_B = i;
  #ifdef DEBUG_INFO
      Serial.print(float(g_Battery_Voltage) *0.01); ///<打印电压
      Serial.print(",");
      Serial.print(Motorb); ///<打印电机输入PWM
      Serial.print(",");
      Serial.print(Target_B); ///<打印目标速度
      Serial.print(",");
      Serial.println(Velocity_B); ///<打印车轮速度，可使用串口绘图器查看
  #endif
    delay(1000);
  }
}
