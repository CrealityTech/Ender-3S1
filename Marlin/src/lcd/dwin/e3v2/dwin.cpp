/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/**
 * DWIN by Creality3D
 */

#include "../../../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_CREALITY_LCD)

#include "dwin.h"

#if ANY(AUTO_BED_LEVELING_BILINEAR, AUTO_BED_LEVELING_LINEAR, AUTO_BED_LEVELING_3POINT) && DISABLED(PROBE_MANUALLY)
  #define HAS_ONESTEP_LEVELING 1
#endif

#if ANY(BABYSTEPPING, HAS_BED_PROBE, HAS_WORKSPACE_OFFSET)
  #define HAS_ZOFFSET_ITEM 1
#endif

#if !HAS_BED_PROBE && ENABLED(BABYSTEPPING)
  #define JUST_BABYSTEP 1
#endif

#include <WString.h>
#include <stdio.h>
#include <string.h>

#include "../../fontutils.h"
#include "../../marlinui.h"

#include "../../../sd/cardreader.h"

#include "../../../MarlinCore.h"
#include "../../../core/serial.h"
#include "../../../core/macros.h"
#include "../../../gcode/queue.h"

#include "../../../module/temperature.h"
#include "../../../module/printcounter.h"
#include "../../../module/motion.h"
#include "../../../module/planner.h"

#if ENABLED(EEPROM_SETTINGS)
  #include "../../../module/settings.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../../feature/host_actions.h"
#endif

#if HAS_ONESTEP_LEVELING
  #include "../../../feature/bedlevel/bedlevel.h"
#endif

#if HAS_BED_PROBE
  #include "../../../module/probe.h"
#endif

#if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
  #include "../../../feature/babystep.h"
#endif

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../../feature/powerloss.h"
#endif

#ifndef MACHINE_SIZE
  #define MACHINE_SIZE STRINGIFY(X_BED_SIZE) "x" STRINGIFY(Y_BED_SIZE) "x" STRINGIFY(Z_MAX_POS)
#endif
#ifndef CORP_WEBSITE
  #define CORP_WEBSITE WEBSITE_URL
#endif

#define CORP_WEBSITE_C    "www.cxsw3d.com"
#define CORP_WEBSITE_E    "www.creality.com"
#define PAUSE_HEAT
#define CHECKFILAMENT true

#define USE_STRING_HEADINGS
//#define USE_STRING_TITLES

#define DWIN_FONT_MENU font8x16
#define DWIN_FONT_STAT font10x20
#define DWIN_FONT_HEAD font10x20

#define MENU_CHAR_LIMIT  24
#define STATUS_Y        360

// Print speed limit
#define MIN_PRINT_SPEED  10
#define MAX_PRINT_SPEED 999

// Feedspeed limit (max feedspeed = DEFAULT_MAX_FEEDRATE * 2)
#define MIN_MAXFEEDSPEED      1
#define MIN_MAXACCELERATION   1
#define MIN_MAXJERK           0.1
#define MIN_STEP              1

#define FEEDRATE_E      (60)

// Minimum unit (0.1) : multiple (10)
#define UNITFDIGITS 1
#define MINUNITMULT pow(10, UNITFDIGITS)

#define ENCODER_WAIT_MS                  20
#define DWIN_VAR_UPDATE_INTERVAL         1024  
#define DWIN_SCROLL_UPDATE_INTERVAL      SEC_TO_MS(0.5)  //rock_20210819
#define DWIN_REMAIN_TIME_UPDATE_INTERVAL SEC_TO_MS(20)

constexpr uint16_t TROWS = 6, MROWS = TROWS - 1,        // Total rows, and other-than-Back
                   TITLE_HEIGHT = 30,                   // Title bar height
                   MLINE = 53,                          // Menu line height
                   LBLX = 58,                           // Menu item label X  
                   MENU_CHR_W = 8, STAT_CHR_W = 10;

#define MBASE(L) (49 + MLINE * (L))

#define BABY_Z_VAR TERN(HAS_BED_PROBE, probe.offset.z, dwin_zoffset)

/* Value Init */
HMI_value_t HMI_ValueStruct;
HMI_Flag_t HMI_flag{0};
CRec CardRecbuf;//rock_20211021
millis_t dwin_heat_time = 0;

uint8_t checkkey = 0;
static bool temp_remove_card_flag=false,temp_cutting_line_flag=false/*,temp_wifi_print_flag=false*/; /*0 沒有中断， 1 断料暂停  2 拔卡暂停*/
typedef struct {
  uint8_t now, last;
  void set(uint8_t v) { now = last = v; }
  void reset() { set(0); }
  bool changed() { bool c = (now != last); if (c) last = now; return c; }
  bool dec() { if (now) now--; return changed(); }
  bool inc(uint8_t v) { if (now < (v - 1)) now++; else now = (v - 1); return changed(); }
} select_t;

typedef struct
{
  char filename[FILENAME_LENGTH];
  char longfilename[LONG_FILENAME_LENGTH];
} PrintFile_InfoTypeDef;


select_t select_page{0}, select_file{0}, select_print{0}, select_prepare{0}
         , select_control{0}, select_axis{0}, select_temp{0}, select_motion{0}, select_tune{0}
         , select_advset{0}, select_PLA{0}, select_ABS{0}
         , select_speed{0}
         , select_acc{0}
         , select_jerk{0}
         , select_step{0}
         , select_item{0}
         ;

uint8_t index_file     = MROWS,
        index_prepare  = MROWS,
        index_control  = MROWS,
        index_leveling = MROWS,
        index_tune     = MROWS,
        index_advset   = MROWS;

bool dwin_abort_flag = false; // Flag to reset feedrate, return to Home

constexpr float default_max_feedrate[]        = DEFAULT_MAX_FEEDRATE;
constexpr float default_max_acceleration[]    = DEFAULT_MAX_ACCELERATION;

#if HAS_CLASSIC_JERK
  constexpr float default_max_jerk[]          = { DEFAULT_XJERK, DEFAULT_YJERK, DEFAULT_ZJERK, DEFAULT_EJERK };
#endif

static uint8_t _card_percent = 0;
uint8_t Cloud_Progress_Bar=0; //云打印传输的进度条数据
uint32_t _remain_time = 0;    //rock_20210830

#if ENABLED(PAUSE_HEAT)
  #if ENABLED(HAS_HOTEND)
    uint16_t resume_hotend_temp = 0;
  #endif
  #if ENABLED(HAS_HEATED_BED)
    uint16_t resume_bed_temp = 0;
  #endif
#endif

#if HAS_ZOFFSET_ITEM
  float dwin_zoffset = 0, last_zoffset = 0;
#endif

#define DWIN_LANGUAGE_EEPROM_ADDRESS 0x01   // Between 0x01 and 0x63 (EEPROM_OFFSET-1)
                                            // BL24CXX::check() uses 0x00

/*获取指定g文件信息 *short_file_name:短文件名 *file:文件信息指针 返回值*/
void get_file_info(char *short_file_name, PrintFile_InfoTypeDef *file)
{
  if(!card.isMounted()){
    return;
  }

  card.getWorkDirName(); // 获取根目录
  if(card.filename[0] != '/')
  {
    card.cdup();
  }
	card.selectFileByName(short_file_name); // 选择文件
	// 获取gcode文件信息
  strcpy(file->filename, card.filename);
  strcpy(file->longfilename, card.longFilename);
}

inline bool HMI_IsChinese() { return HMI_flag.language == DWIN_CHINESE; }

void HMI_SetLanguageCache() 
{
  DWIN_JPG_CacheTo1(HMI_IsChinese() ? Language_Chinese : Language_English); 
}
void HMI_ResetLanguage()
{
  if(HMI_IsChinese())
  {
    HMI_flag.language=DWIN_ENGLISH;
    BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  }  
  HMI_SetLanguageCache();
}

void HMI_SetLanguage() {
  #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
    BL24CXX::read(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  #endif
  if((HMI_flag.language!=DWIN_ENGLISH) && (HMI_flag.language!=DWIN_CHINESE)) //rock_901122 解决第一次上电 语言混乱的问题
  {
    HMI_flag.language=DWIN_ENGLISH;
    BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  }
  HMI_SetLanguageCache();
}

void HMI_ToggleLanguage() 
{
  HMI_flag.language = HMI_IsChinese() ? DWIN_ENGLISH : DWIN_CHINESE;  //
  HMI_SetLanguageCache();
  #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
    BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  #endif
}

void DWIN_Draw_Signed_Float(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value) {
  if (value < 0) 
  {
    DWIN_Draw_String(false, true, size, Color_White, bColor, x - 6, y, F("-"));
    DWIN_Draw_FloatValue(true, true, 0, size, Color_White, bColor, iNum, fNum, x, y, -value);
  }
  else 
  {
    DWIN_Draw_String(false, true, size, Color_White, bColor, x - 6, y, F(" "));
    DWIN_Draw_FloatValue(true, true, 0, size, Color_White, bColor, iNum, fNum, x, y, value);
  }
}

void ICON_Print() {
  if (select_page.now == 0) {
    DWIN_ICON_Show(ICON, ICON_Print_1, 17, 130);
    DWIN_Draw_Rectangle(0, Color_White, 17, 130, 126, 229);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 447, 28, 460, 58, 201);
    else
      DWIN_Frame_AreaCopy(1, 1, 451, 31, 463, 57, 201);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Print_0, 17, 130);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 405, 28, 420, 58, 201);
    else
      DWIN_Frame_AreaCopy(1, 1, 423, 31, 435, 57, 201);
  }
}

void ICON_Prepare() {
  if (select_page.now == 1) {
    DWIN_ICON_Show(ICON, ICON_Prepare_1, 145, 130);
    DWIN_Draw_Rectangle(0, Color_White, 145, 130, 254, 229);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 31, 447, 58, 460, 186, 201);
    else
      DWIN_Frame_AreaCopy(1, 33, 451, 82, 466, 175, 201);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Prepare_0, 145, 130);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 31, 405, 58, 420, 186, 201);
    else
      DWIN_Frame_AreaCopy(1, 33, 423, 82, 438, 175, 201);
  }
}

void ICON_Control() {
  if (select_page.now == 2) {
    DWIN_ICON_Show(ICON, ICON_Control_1, 17, 246);
    DWIN_Draw_Rectangle(0, Color_White, 17, 246, 126, 345);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 61, 447, 88, 460, 58, 318);
    else
      DWIN_Frame_AreaCopy(1, 85, 451, 132, 463, 48, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Control_0, 17, 246);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 61, 405, 88, 420, 58, 318);
    else
      DWIN_Frame_AreaCopy(1, 85, 423, 132, 434, 48, 318);
  }
}

void ICON_StartInfo(bool show) {
  if (show) {
    DWIN_ICON_Show(ICON, ICON_Info_1, 145, 246);
    DWIN_Draw_Rectangle(0, Color_White, 145, 246, 254, 345);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 91, 447, 118, 460, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 132, 451, 159, 466, 186, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Info_0, 145, 246);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 91, 405, 118, 420, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 132, 423, 159, 435, 186, 318);
  }
}

//rock_20210726
void ICON_Leveling(bool show) {
  if (show) {
    DWIN_ICON_Show(ICON, ICON_Leveling_1, 145, 246);
    DWIN_Draw_Rectangle(0, Color_White, 145, 246, 254, 345);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 211, 447, 238, 460, 186, 318);
    else 
      DWIN_Frame_AreaCopy(1, 84, 465, 120,  478, 182, 318); //rock 
      //DWIN_Frame_AreaCopy(1, 84, 437, 120,  449, 182, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Leveling_0, 145, 246);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 211, 405, 238, 420, 186, 318);
    else 
      DWIN_Frame_AreaCopy(1, 84, 437, 120,  449, 182, 318);
      //DWIN_Frame_AreaCopy(1, 84, 465, 120, 478, 182, 318);
  }
}

void ICON_Tune() {
  if (select_print.now == 0) {
    DWIN_ICON_Show(ICON, ICON_Setup_1, 8, 252);
    DWIN_Draw_Rectangle(0, Color_White, 8, 252, 87, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 121, 447, 148, 458, 34, 325);
    else
      DWIN_Frame_AreaCopy(1,   0, 466,  34, 476, 31, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Setup_0, 8, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 121, 405, 148, 420, 34, 325);
    else
      DWIN_Frame_AreaCopy(1,   0, 438,  32, 448, 31, 325);
  }
}
//打印暂停
void ICON_Pause() 
{
  if (select_print.now == 1) {
    DWIN_ICON_Show(ICON, ICON_Pause_1, 96, 252);
    DWIN_Draw_Rectangle(0, Color_White, 96, 252, 175, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 181, 447, 208, 459, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 177, 451, 216, 462, 116, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Pause_0, 96, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 181, 405, 208, 420, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 177, 423, 215, 433, 116, 325);
  }
}
//继续打印
void ICON_Continue() {
  if (select_print.now == 1) 
  {
    DWIN_ICON_Show(ICON, ICON_Continue_1, 96, 252);
    DWIN_Draw_Rectangle(0, Color_White, 96, 252, 175, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 447, 28, 460, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 1, 452, 32, 464, 121, 325);
  }
  else 
  {
    DWIN_ICON_Show(ICON, ICON_Continue_0, 96, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 405, 28, 420, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 1, 424, 31, 434, 121, 325);
  }
}

void ICON_Stop() {
  if (select_print.now == 2) {
    DWIN_ICON_Show(ICON, ICON_Stop_1, 184, 252);
    DWIN_Draw_Rectangle(0, Color_White, 184, 252, 263, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 151, 447, 178, 459, 210, 325);
    else
      DWIN_Frame_AreaCopy(1, 218, 451, 249, 468, 209, 325);  //rock_20210727
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Stop_0, 184, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 151, 405, 178, 420, 210, 325);
    else
      DWIN_Frame_AreaCopy(1, 218, 423, 247, 436, 209, 325);
  }
}

void Clear_Title_Bar() {
  DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, DWIN_WIDTH, 30);
}
void Clear_Remain_Time() 
{
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 176, 212,200+20, 212+30);
}
void Clear_Print_Time() 
{
  DWIN_Draw_Rectangle(1, Color_Bg_Black,  42, 212, 66 + 20, 212+30);
}
void Draw_Title(const char * const title) {
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

void Draw_Title(const __FlashStringHelper * title) {
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

void Clear_Menu_Area() {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, STATUS_Y - 1);
}

void Clear_Main_Window() {
  Clear_Title_Bar();
  Clear_Menu_Area();
}

void Clear_Popup_Area() {
  Clear_Title_Bar();
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, DWIN_HEIGHT);
}

void Draw_Popup_Bkgd_105() {
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 105, 258, 374);
}

void Draw_More_Icon(const uint8_t line) {
  DWIN_ICON_Show(ICON, ICON_More, 226, MBASE(line) - 3);
}

void Draw_Menu_Cursor(const uint8_t line) {
  // DWIN_ICON_Show(ICON,ICON_Rectangle, 0, MBASE(line) - 18);
  DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
}

void Erase_Menu_Cursor(const uint8_t line) {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
}

void Move_Highlight(const int16_t from, const uint16_t newline) {
  Erase_Menu_Cursor(newline - from);
  Draw_Menu_Cursor(newline);
}

void Add_Menu_Line() {
  Move_Highlight(1, MROWS);
  DWIN_Draw_Line(Line_Color, 16, MBASE(MROWS + 1) - 20, 256, MBASE(MROWS + 1) - 19);
}

void Scroll_Menu(const uint8_t dir) {
  DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
  switch (dir) {
    case DWIN_SCROLL_DOWN: Move_Highlight(-1, 0); break;
    case DWIN_SCROLL_UP:   Add_Menu_Line(); break;
  }
}

inline uint16_t nr_sd_menu_items() {
  return card.get_num_Files() + !card.flag.workDirIsRoot;
}

void Draw_Menu_Icon(const uint8_t line, const uint8_t icon) {
  DWIN_ICON_Show(ICON, icon, 26, MBASE(line) - 3);
}

void Erase_Menu_Text(const uint8_t line) {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(line) - 14, 271, MBASE(line) + 28);
}

void Draw_Menu_Item(const uint8_t line, const uint8_t icon=0, const char * const label=nullptr, bool more=false) {
  if (label) DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(line) - 1, (char*)label);
  if (icon) Draw_Menu_Icon(line, icon);
  if (more) Draw_More_Icon(line);
}

void Draw_Menu_Line(const uint8_t line, const uint8_t icon=0, const char * const label=nullptr, bool more=false) {
  Draw_Menu_Item(line, icon, label, more);
  DWIN_Draw_Line(Line_Color, 16, MBASE(line) + 33, 256, MBASE(line) + 34);
}

void Draw_Chkb_Line(const uint8_t line, const bool mode) {
  DWIN_Draw_Checkbox(Color_White, Color_Bg_Black, 225, MBASE(line) - 1, mode);
}

// The "Back" label is always on the first line
void Draw_Back_Label() {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 129, 72, 156, 84, LBLX, MBASE(0));
  else
    DWIN_Frame_AreaCopy(1, 226, 179, 256, 189, LBLX, MBASE(0));
}

// Draw "Back" line at the top
void Draw_Back_First(const bool is_sel=true) {
  Draw_Menu_Line(0, ICON_Back);
  Draw_Back_Label();
  if (is_sel) Draw_Menu_Cursor(0);
}

inline bool Apply_Encoder(const ENCODER_DiffState &encoder_diffState, auto &valref) {
  bool temp_var=false;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {    
    valref += EncoderRate.encoderMoveValue;
    if(valref>(EXTRUDE_MAXLENGTH_e * MINUNITMULT))valref=(EXTRUDE_MAXLENGTH_e * MINUNITMULT);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
    valref -= EncoderRate.encoderMoveValue;
  else if(encoder_diffState == ENCODER_DIFF_ENTER)
  {
    temp_var=true;
  }
  return temp_var; 
}

//
// Draw Menus
//

#define MOTION_CASE_RATE   1
#define MOTION_CASE_ACCEL  2
#define MOTION_CASE_JERK   (MOTION_CASE_ACCEL + ENABLED(HAS_CLASSIC_JERK))
#define MOTION_CASE_STEPS  (MOTION_CASE_JERK + 1)
#define MOTION_CASE_TOTAL  MOTION_CASE_STEPS

#define PREPARE_CASE_MOVE  1
#define PREPARE_CASE_DISA  2
#define PREPARE_CASE_HOME  3
#define PREPARE_CASE_ZOFF (PREPARE_CASE_HOME + ENABLED(HAS_ZOFFSET_ITEM))
#define PREPARE_CASE_PLA  (PREPARE_CASE_ZOFF + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_ABS  (PREPARE_CASE_PLA + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_COOL (PREPARE_CASE_ABS + EITHER(HAS_HOTEND, HAS_HEATED_BED))
#define PREPARE_CASE_LANG (PREPARE_CASE_COOL + 1)
#define PREPARE_CASE_TOTAL PREPARE_CASE_LANG

#define CONTROL_CASE_TEMP 1
#define  CONTROL_CASE_MOVE  (CONTROL_CASE_TEMP + 1)
#define CONTROL_CASE_SAVE  (CONTROL_CASE_MOVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_LOAD  (CONTROL_CASE_SAVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_RESET (CONTROL_CASE_LOAD + ENABLED(EEPROM_SETTINGS))

//#define CONTROL_CASE_ADVSET (CONTROL_CASE_RESET + 1)  //rock_20210726
//#define CONTROL_CASE_INFO  (CONTROL_CASE_ADVSET + 1)
#define CONTROL_CASE_INFO  (CONTROL_CASE_RESET + 1)
#define CONTROL_CASE_TOTAL CONTROL_CASE_INFO

#define TUNE_CASE_SPEED 1
#define TUNE_CASE_TEMP (TUNE_CASE_SPEED + ENABLED(HAS_HOTEND))
#define TUNE_CASE_BED  (TUNE_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TUNE_CASE_FAN  (TUNE_CASE_BED + ENABLED(HAS_FAN))
#define TUNE_CASE_ZOFF (TUNE_CASE_FAN + ENABLED(HAS_ZOFFSET_ITEM))
#define TUNE_CASE_TOTAL TUNE_CASE_ZOFF

#define TEMP_CASE_TEMP (0 + ENABLED(HAS_HOTEND))
#define TEMP_CASE_BED  (TEMP_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TEMP_CASE_FAN  (TEMP_CASE_BED + ENABLED(HAS_FAN))
#define TEMP_CASE_PLA  (TEMP_CASE_FAN + ENABLED(HAS_HOTEND))
#define TEMP_CASE_ABS  (TEMP_CASE_PLA + ENABLED(HAS_HOTEND))
#define TEMP_CASE_TOTAL TEMP_CASE_ABS

#define PREHEAT_CASE_TEMP (0 + ENABLED(HAS_HOTEND))
#define PREHEAT_CASE_BED  (PREHEAT_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define PREHEAT_CASE_FAN  (PREHEAT_CASE_BED + ENABLED(HAS_FAN))
#define PREHEAT_CASE_SAVE (PREHEAT_CASE_FAN + ENABLED(EEPROM_SETTINGS))
#define PREHEAT_CASE_TOTAL PREHEAT_CASE_SAVE

#define ADVSET_CASE_HOMEOFF   1
#define ADVSET_CASE_PROBEOFF  (ADVSET_CASE_HOMEOFF + ENABLED(HAS_ONESTEP_LEVELING))
#define ADVSET_CASE_HEPID     (ADVSET_CASE_PROBEOFF + ENABLED(HAS_HOTEND))
#define ADVSET_CASE_BEDPID    (ADVSET_CASE_HEPID + ENABLED(HAS_HEATED_BED))
#define ADVSET_CASE_PWRLOSSR  (ADVSET_CASE_BEDPID + ENABLED(POWER_LOSS_RECOVERY))
#define ADVSET_CASE_TOTAL     ADVSET_CASE_PWRLOSSR


//暂停恢复前的进料动作。
#define FEEDING_DEF_DISTANCE                10                /* in material: default distance of feeding material */
#define FEEDING_DEF_SPEED                   3                   // in material: default speed of feeding

//pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
static void pause_resume_feedstock(uint16_t _distance,uint16_t _feedRate)
{
   char cmd[20], str_1[16];
  current_position[E_AXIS] += _distance;
  line_to_current_position(feedRate_t(_feedRate));
  current_position[E_AXIS] -= _distance;
  memset(cmd,0,sizeof(cmd));
  sprintf_P(cmd, PSTR("G92.9E%s"), dtostrf(current_position[E_AXIS], 1, 3, str_1));
  gcode.process_subcommands_now(cmd);
  memset(cmd,0,sizeof(cmd));
     // Resume the feedrate
  sprintf_P(cmd, PSTR("G1F%d"), MMS_TO_MMM(feedrate_mm_s));
  gcode.process_subcommands_now(cmd);
}
//
// Draw Menus
//

void DWIN_Draw_Label(const uint16_t y, char *string) {
  DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, y, string);
}
void DWIN_Draw_Label(const uint16_t y, const __FlashStringHelper *title) {
  DWIN_Draw_Label(y, (char*)title);
}

void draw_move_en(const uint16_t line) {
  #ifdef USE_STRING_TITLES
    DWIN_Draw_Label(line, F("Move"));
  #else
    DWIN_Frame_AreaCopy(1, 69, 61, 102, 71, LBLX, line); // "Move"
  #endif
}

void DWIN_Frame_TitleCopy(uint8_t id, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) { DWIN_Frame_AreaCopy(id, x1, y1, x2, y2, 14, 8); }
void Item_Prepare_Move(const uint8_t row) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 159, 70, 200, 84, LBLX, MBASE(row));
  else
    draw_move_en(MBASE(row)); // "Move"
  Draw_Menu_Line(row, ICON_Axis);
  Draw_More_Icon(row);
}

void Item_Prepare_Disable(const uint8_t row) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 204, 70, 259, 82, LBLX, MBASE(row));
  else 
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_DISABLE_STEPPERS));
    #else
      DWIN_Frame_AreaCopy(1, 103, 59, 200, 74, LBLX, MBASE(row)); // "Disable Stepper"
    #endif
  }
  Draw_Menu_Line(row, ICON_CloseMotor);
}

void Item_Prepare_Home(const uint8_t row) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 0, 89, 41, 101, LBLX, MBASE(row));
  else 
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_AUTO_HOME));
    #else
      DWIN_Frame_AreaCopy(1, 202, 61, 271, 71, LBLX, MBASE(row)); // "Auto Home"
    #endif
  }
  Draw_Menu_Line(row, ICON_Homing);
}

#if HAS_ZOFFSET_ITEM

  void Item_Prepare_Offset(const uint8_t row) {
    if (HMI_IsChinese()) {
      #if HAS_BED_PROBE
        DWIN_Frame_AreaCopy(1, 174, 164, 223, 177, LBLX, MBASE(row));
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(row), probe.offset.z * 100);
      #else
        DWIN_Frame_AreaCopy(1, 43, 89, 98, 101, LBLX, MBASE(row));
      #endif
    }
    else {
      #if HAS_BED_PROBE
        #ifdef USE_STRING_TITLES
          DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_ZPROBE_ZOFFSET));
        #else
          DWIN_Frame_AreaCopy(1, 93, 179, 141, 189, LBLX, MBASE(row));    // "Z-Offset"
        #endif
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(row), probe.offset.z * 100);
      #else
        #ifdef USE_STRING_TITLES
          DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_SET_HOME_OFFSETS));
        #else
          DWIN_Frame_AreaCopy(1, 1, 76, 106, 86, LBLX, MBASE(row));       // "Set home offsets"
        #endif
      #endif
    }
    Draw_Menu_Line(row, ICON_SetHome);
  }

#endif

#if HAS_HOTEND
  void Item_Prepare_PLA(const uint8_t row) {
    if (HMI_IsChinese()) {
      DWIN_Frame_AreaCopy(1, 100, 89, 151, 101, LBLX, MBASE(row));
    }
    else {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("Preheat " PREHEAT_1_LABEL));
      #else
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(row));       // "Preheat"
        DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX + 52, MBASE(row));  // "PLA"
      #endif
    }
    Draw_Menu_Line(row, ICON_PLAPreheat);
  }

  void Item_Prepare_ABS(const uint8_t row) {
    if (HMI_IsChinese()) {
      DWIN_Frame_AreaCopy(1, 180, 89, 233, 100, LBLX, MBASE(row));
    }
    else {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("Preheat " PREHEAT_2_LABEL));
      #else
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(row));       // "Preheat"
        DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX + 52, MBASE(row));  // "ABS"
      #endif
    }
    Draw_Menu_Line(row, ICON_ABSPreheat);
  }
#endif

#if HAS_PREHEAT
  void Item_Prepare_Cool(const uint8_t row) {
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1,   1, 104,  56, 117, LBLX, MBASE(row));
    else {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_COOLDOWN));
      #else
        DWIN_Frame_AreaCopy(1, 200,  76, 264,  86, LBLX, MBASE(row));      // "Cooldown"
      #endif
    }
    Draw_Menu_Line(row, ICON_Cool);
  }
#endif

void Item_Prepare_Lang(const uint8_t row) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 239, 134, 266, 146, LBLX, MBASE(row));
  else {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(row), F("UI Language"));
    #else
      DWIN_Frame_AreaCopy(1, 0, 194, 121, 207, LBLX, MBASE(row)); // "Language selection"
    #endif
  }
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 226, MBASE(row), HMI_IsChinese() ? F("CN") : F("EN"));
  Draw_Menu_Icon(row, ICON_Language);
}

void Draw_Prepare_Menu() {
  Clear_Main_Window();

  const int16_t scroll = MROWS - index_prepare; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 133, 1, 160, 13);   // "Prepare"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_PREPARE));
    #else
      DWIN_Frame_TitleCopy(1, 178, 2, 229, 14); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_prepare.now == 0);                         // < Back
  if (PVISI(PREPARE_CASE_MOVE)) Item_Prepare_Move(PSCROL(PREPARE_CASE_MOVE));     // Move >
  if (PVISI(PREPARE_CASE_DISA)) Item_Prepare_Disable(PSCROL(PREPARE_CASE_DISA));  // Disable Stepper
  if (PVISI(PREPARE_CASE_HOME)) Item_Prepare_Home(PSCROL(PREPARE_CASE_HOME));     // Auto Home
  #if HAS_ZOFFSET_ITEM
    if (PVISI(PREPARE_CASE_ZOFF)) Item_Prepare_Offset(PSCROL(PREPARE_CASE_ZOFF)); // Edit Z-Offset / Babystep / Set Home Offset
  #endif
  #if HAS_HOTEND
    if (PVISI(PREPARE_CASE_PLA)) Item_Prepare_PLA(PSCROL(PREPARE_CASE_PLA));      // Preheat PLA
    if (PVISI(PREPARE_CASE_ABS)) Item_Prepare_ABS(PSCROL(PREPARE_CASE_ABS));      // Preheat ABS
  #endif
  #if HAS_PREHEAT
    if (PVISI(PREPARE_CASE_COOL)) Item_Prepare_Cool(PSCROL(PREPARE_CASE_COOL));   // Cooldown
  #endif
  if (PVISI(PREPARE_CASE_LANG)) Item_Prepare_Lang(PSCROL(PREPARE_CASE_LANG));     // Language CN/EN

  if (select_prepare.now) Draw_Menu_Cursor(PSCROL(select_prepare.now));
}

void Item_Control_Info(const uint16_t line) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 231, 102, 258, 116, LBLX, line);  //rock_20210726
  else 
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(line, F("Info"));
    #else
      DWIN_Frame_AreaCopy(1, 0, 104, 24, 114, LBLX, line);
    #endif
  }
}

void Draw_Control_Menu() {
  Clear_Main_Window();

  #if CONTROL_CASE_TOTAL >= 6
    const int16_t scroll = MROWS - index_control; // Scrolled-up lines
    #define CSCROL(L) (scroll + (L))
  #else
    #define CSCROL(L) (L)
  #endif
  #define CLINE(L)  MBASE(CSCROL(L))
  #define CVISI(L)  WITHIN(CSCROL(L), 0, MROWS)

  if (CVISI(0)) Draw_Back_First(select_control.now == 0);                         // < Back

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 103, 1, 130, 14);                                     // "Control"

    DWIN_Frame_AreaCopy(1,  57, 104,  84, 116, LBLX, CLINE(CONTROL_CASE_TEMP));   // Temperature >
    DWIN_Frame_AreaCopy(1,  87, 104, 114, 116, LBLX, CLINE(CONTROL_CASE_MOVE));   // Motion >

    #if ENABLED(EEPROM_SETTINGS)
      DWIN_Frame_AreaCopy(1, 117, 104, 172, 116, LBLX, CLINE(CONTROL_CASE_SAVE));   // Store Configuration  
      DWIN_Frame_AreaCopy(1, 174, 103, 229, 116, LBLX, CLINE(CONTROL_CASE_LOAD));   // Read Configuration
      DWIN_Frame_AreaCopy(1,   1, 118,  56, 131, LBLX, CLINE(CONTROL_CASE_RESET));  // Reset Configuration
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_CONTROL));
    #else
      DWIN_Frame_TitleCopy(1, 128, 2, 176, 12);                                         // "Control"
    #endif
    #ifdef USE_STRING_TITLES
      if (CVISI(CONTROL_CASE_TEMP)) DWIN_Draw_Label(CLINE(CONTROL_CASE_TEMP), GET_TEXT_F(MSG_TEMPERATURE));
      if (CVISI(CONTROL_CASE_MOVE)) DWIN_Draw_Label(CLINE(CONTROL_CASE_MOVE), GET_TEXT_F(MSG_MOTION));
      #if ENABLED(EEPROM_SETTINGS)
        if (CVISI(CONTROL_CASE_SAVE)) DWIN_Draw_Label(CLINE(CONTROL_CASE_SAVE), GET_TEXT_F(MSG_STORE_EEPROM));
        if (CVISI(CONTROL_CASE_LOAD)) DWIN_Draw_Label(CLINE(CONTROL_CASE_LOAD), GET_TEXT_F(MSG_LOAD_EEPROM));
        if (CVISI(CONTROL_CASE_RESET)) DWIN_Draw_Label(CLINE(CONTROL_CASE_RESET), GET_TEXT_F(MSG_RESTORE_DEFAULTS));
      #endif
    #else
      if (CVISI(CONTROL_CASE_TEMP)) DWIN_Frame_AreaCopy(1,  1, 89,  83, 101, LBLX, CLINE(CONTROL_CASE_TEMP));           // Temperature >
      if (CVISI(CONTROL_CASE_MOVE)) DWIN_Frame_AreaCopy(1, 84, 89, 128,  99, LBLX, CLINE(CONTROL_CASE_MOVE));           // Motion >
      #if ENABLED(EEPROM_SETTINGS)
        if (CVISI(CONTROL_CASE_SAVE)) DWIN_Frame_AreaCopy(1, 147,  89, 268, 101, LBLX  , CLINE(CONTROL_CASE_SAVE));  // "Store Configuration" rock_20210923
        if (CVISI(CONTROL_CASE_LOAD))  
        {
          DWIN_Frame_AreaCopy(1,  26, 104,  57, 114, LBLX     , CLINE(CONTROL_CASE_LOAD));      // "Read"
          DWIN_Frame_AreaCopy(1, 182,  89, 268, 101, LBLX + 34, CLINE(CONTROL_CASE_LOAD));      // "Configuration"
        }
        if (CVISI(CONTROL_CASE_RESET)) 
        {
          DWIN_Frame_AreaCopy(1,  59, 104,  93, 114, LBLX     , CLINE(CONTROL_CASE_RESET)); // "Reset"
          DWIN_Frame_AreaCopy(1, 182,  89, 268, 101, LBLX + 37, CLINE(CONTROL_CASE_RESET)); // "Configuration"
        }
      #endif
    #endif
  }
/*
  if (CVISI(CONTROL_CASE_ADVSET)) {
    //DWIN_Draw_Label(CLINE(CONTROL_CASE_ADVSET), GET_TEXT_F(MSG_ADVANCED_SETTINGS));  // Advanced Settings
  }
*/
  if (CVISI(CONTROL_CASE_INFO)) Item_Control_Info(CLINE(CONTROL_CASE_INFO));

  if (select_control.now && CVISI(select_control.now))
    Draw_Menu_Cursor(CSCROL(select_control.now));

  // Draw icons and lines
  #define _TEMP_ICON(N, I, M) do { \
    if (CVISI(N)) { \
      Draw_Menu_Line(CSCROL(N), I); \
      if (M) { \
        Draw_More_Icon(CSCROL(N)); \
      } \
    } \
  } while(0)

  _TEMP_ICON(CONTROL_CASE_TEMP, ICON_Temperature, true);
  _TEMP_ICON(CONTROL_CASE_MOVE, ICON_Motion, true);

  #if ENABLED(EEPROM_SETTINGS)
    _TEMP_ICON(CONTROL_CASE_SAVE, ICON_WriteEEPROM, false);
    _TEMP_ICON(CONTROL_CASE_LOAD, ICON_ReadEEPROM, false);
    _TEMP_ICON(CONTROL_CASE_RESET, ICON_ResumeEEPROM, false);
  #endif

  //_TEMP_ICON(CONTROL_CASE_ADVSET, ICON_AdvSet, true);  //rock_20210724
  _TEMP_ICON(CONTROL_CASE_INFO, ICON_Info, true);
}

void Draw_Tune_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 73, 2, 100, 13, 14, 9);
    DWIN_Frame_AreaCopy(1, 116, 164, 171, 176, LBLX, MBASE(TUNE_CASE_SPEED));
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 1, 134, 56, 146, LBLX, MBASE(TUNE_CASE_TEMP));
    #endif
    #if HAS_HEATED_BED
      DWIN_Frame_AreaCopy(1, 58, 134, 113, 146, LBLX, MBASE(TUNE_CASE_BED));
    #endif
    #if HAS_FAN
      DWIN_Frame_AreaCopy(1, 115, 134, 172, 146, LBLX, MBASE(TUNE_CASE_FAN));
    #endif
    #if HAS_ZOFFSET_ITEM
      DWIN_Frame_AreaCopy(1, 174, 164, 223, 177, LBLX, MBASE(TUNE_CASE_ZOFF));
    #endif
  }
  else 
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_TUNE));
    #else
      DWIN_Frame_AreaCopy(1, 94, 2, 126, 12, 14, 9);
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(TUNE_CASE_SPEED), GET_TEXT_F(MSG_SPEED));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(TUNE_CASE_TEMP), GET_TEXT_F(MSG_UBL_SET_TEMP_HOTEND));
      #endif
      #if HAS_HEATED_BED
        DWIN_Draw_Label(MBASE(TUNE_CASE_BED), GET_TEXT_F(MSG_UBL_SET_TEMP_BED));
      #endif
      #if HAS_FAN
        DWIN_Draw_Label(MBASE(TUNE_CASE_FAN), GET_TEXT_F(MSG_FAN_SPEED));
      #endif
      DWIN_Draw_Label(MBASE(TUNE_CASE_ZOFF), GET_TEXT_F(MSG_ZPROBE_ZOFFSET));
    #else
      DWIN_Frame_AreaCopy(1, 1, 179, 92, 192, LBLX, MBASE(TUNE_CASE_SPEED));      // Print speed
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 197, 104, 240, 114, LBLX, MBASE(TUNE_CASE_TEMP));  // Hotend... rock_20210907
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 44, MBASE(TUNE_CASE_TEMP)); // ...Temperature
      #endif
      #if HAS_HEATED_BED
        DWIN_Frame_AreaCopy(1, 240, 104, 264, 114, LBLX, MBASE(TUNE_CASE_BED));   // Bed...
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 27, MBASE(TUNE_CASE_BED));  // ...Temperature
      #endif
      #if HAS_FAN
        DWIN_Frame_AreaCopy(1, 0, 119, 64, 132, LBLX, MBASE(TUNE_CASE_FAN));      // Fan speed
      #endif
      #if HAS_ZOFFSET_ITEM
        DWIN_Frame_AreaCopy(1, 93, 179, 141, 189, LBLX, MBASE(TUNE_CASE_ZOFF));   // Z-offset
      #endif
    #endif
   }
    //显示数据
  Draw_Back_First(select_tune.now == 0);
  if (select_tune.now) Draw_Menu_Cursor(select_tune.now);

  Draw_Menu_Line(TUNE_CASE_SPEED, ICON_Speed);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_SPEED), feedrate_percentage);

  #if HAS_HOTEND
    Draw_Menu_Line(TUNE_CASE_TEMP, ICON_HotendTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_TEMP), thermalManager.degTargetHotend(0));
  #endif
  #if HAS_HEATED_BED
    Draw_Menu_Line(TUNE_CASE_BED, ICON_BedTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_BED), thermalManager.degTargetBed());
  #endif
  #if HAS_FAN
    Draw_Menu_Line(TUNE_CASE_FAN, ICON_FanSpeed);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_FAN), thermalManager.fan_speed[0]);
  #endif
  #if HAS_ZOFFSET_ITEM
    Draw_Menu_Line(TUNE_CASE_ZOFF, ICON_Zoffset);
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(TUNE_CASE_ZOFF), BABY_Z_VAR * 100);
  #endif
}

void draw_max_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 245, 119, 271, 130, LBLX, line);   // "Max"
}
void draw_max_accel_en(const uint16_t line) {
  draw_max_en(line);
  DWIN_Frame_AreaCopy(1, 1, 135, 79, 145, LBLX + 30, line); // "Acceleration" rock_20210919
}
void draw_speed_en(const uint16_t inset, const uint16_t line) 
{
  DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 30, line); // "Speed"
}
void draw_jerk_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 64, 119, 106, 129, LBLX + 30, line); // "Jerk"
}
void draw_steps_per_mm(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 1, 148, 94, 162, LBLX, line);   // "Steps-per-mm"
}
void say_x(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 95, 104, 102, 114, LBLX + inset, line); // "X"
}
void say_y(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 104, 104, 110, 114, LBLX + inset, line); // "Y"
}
void say_z(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 112, 104, 120, 114, LBLX + inset, line); // "Z"
}
void say_e(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 237, 119, 244, 129, LBLX + inset, line); // "E"
}

void Draw_Motion_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28);                                     // "Motion"
    DWIN_Frame_AreaCopy(1, 173, 133, 228, 147, LBLX, MBASE(MOTION_CASE_RATE));  // Max speed
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_ACCEL));        // Max...
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(MOTION_CASE_ACCEL) + 1); // ...Acceleration
    #if HAS_CLASSIC_JERK
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_JERK));        // Max...
      DWIN_Frame_AreaCopy(1, 1, 180, 28, 192, LBLX + 27, MBASE(MOTION_CASE_JERK) + 1);  // ...
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 54, MBASE(MOTION_CASE_JERK));   // ...Jerk
    #endif
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(MOTION_CASE_STEPS));         // Flow ratio
  }
  else 
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MOTION));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);                                        // "Motion"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(MOTION_CASE_RATE), F("Feedrate"));
      DWIN_Draw_Label(MBASE(MOTION_CASE_ACCEL), GET_TEXT_F(MSG_ACCELERATION));
      #if HAS_CLASSIC_JERK
        DWIN_Draw_Label(MBASE(MOTION_CASE_JERK), GET_TEXT_F(MSG_JERK));
      #endif
      DWIN_Draw_Label(MBASE(MOTION_CASE_STEPS), GET_TEXT_F(MSG_STEPS_PER_MM));
    #else
      draw_max_en(MBASE(MOTION_CASE_RATE)); draw_speed_en(27, MBASE(MOTION_CASE_RATE)); // "Max Speed"
      draw_max_accel_en(MBASE(MOTION_CASE_ACCEL));                                      // "Max Acceleration"
      #if HAS_CLASSIC_JERK
        draw_max_en(MBASE(MOTION_CASE_JERK)); draw_jerk_en(MBASE(MOTION_CASE_JERK));    // "Max Jerk"
      #endif
      draw_steps_per_mm(MBASE(MOTION_CASE_STEPS));                                      // "Steps-per-mm"
    #endif
  }

  Draw_Back_First(select_motion.now == 0);
  if (select_motion.now) Draw_Menu_Cursor(select_motion.now);

  uint8_t i = 0;
  #define _MOTION_ICON(N) Draw_Menu_Line(++i, ICON_MaxSpeed + (N) - 1)
  _MOTION_ICON(MOTION_CASE_RATE); Draw_More_Icon(i);
  _MOTION_ICON(MOTION_CASE_ACCEL); Draw_More_Icon(i);
  #if HAS_CLASSIC_JERK
    _MOTION_ICON(MOTION_CASE_JERK); Draw_More_Icon(i);
  #endif
  _MOTION_ICON(MOTION_CASE_STEPS); Draw_More_Icon(i);
}

//
// Draw Popup Windows
//
#if HAS_HOTEND || HAS_HEATED_BED

  void DWIN_Popup_Temperature(const bool toohigh) {
    Clear_Popup_Area();
    Draw_Popup_Bkgd_105();
    if (toohigh) 
    {
      DWIN_ICON_Show(ICON, ICON_TempTooHigh, 102, 165);
      if (HMI_IsChinese()) 
      {
        DWIN_Frame_AreaCopy(1, 103, 371, 237, 386, 52, 285);
        DWIN_Frame_AreaCopy(1, 151, 389, 185, 402, 187, 285);
        DWIN_Frame_AreaCopy(1, 189, 389, 271, 402, 95, 310);
      }
      else 
      {
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 36, 300, F("Nozzle or Bed temperature"));
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 92, 300, F("is too high"));
      }
    }
    else {
      DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 165);
      if (HMI_IsChinese()) {
        DWIN_Frame_AreaCopy(1, 103, 371, 270, 386, 52, 285);
        DWIN_Frame_AreaCopy(1, 189, 389, 271, 402, 95, 310);
      }
      else {
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 36, 300, F("Nozzle or Bed temperature"));
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 92, 300, F("is too low"));
      }
    }
  }

#endif

void Draw_Popup_Bkgd_60() {
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 258, 330);
}

#if HAS_HOTEND

  void Popup_Window_ETempTooLow() {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 105);
    if (HMI_IsChinese()) {
      DWIN_Frame_AreaCopy(1, 103, 371, 136, 386, 69, 240);
      DWIN_Frame_AreaCopy(1, 170, 371, 270, 386, 102, 240);
      DWIN_ICON_Show(ICON, ICON_Confirm_C, 86, 280);
    }
    else {
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 20, 235, F("Nozzle temperature is too low"));
      DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
    }
  }
#endif

void Popup_Window_Resume() {
  Clear_Popup_Area();
  Draw_Popup_Bkgd_105();
  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 160, 338, 235, 354, 98, 135);
    DWIN_Frame_AreaCopy(1, 103, 321, 271, 335, 52, 192);
    DWIN_ICON_Show(ICON, ICON_Cancel_C,    26, 307);
    DWIN_ICON_Show(ICON, ICON_Continue_C, 146, 307);
  }
  else {
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 14) / 2, 115, F("Continue Print"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 22) / 2, 192, F("It looks like the last"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 22) / 2, 212, F("file was interrupted."));
    DWIN_ICON_Show(ICON, ICON_Cancel_E,    26, 307);
    DWIN_ICON_Show(ICON, ICON_Continue_E, 146, 307);
  }
}

void Popup_Window_Home(const bool parking/*=false*/) {
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  DWIN_ICON_Show(ICON, ICON_BLTouch, 101, 105);
  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 0, 371, 33, 386, 85, 240);
    DWIN_Frame_AreaCopy(1, 203, 286, 271, 302, 118, 240);
    DWIN_Frame_AreaCopy(1, 0, 389, 150, 402, 61, 280);
  }
  else {
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * (parking ? 7 : 10)) / 2, 230, parking ? F("Parking") : F("Homing XYZ"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 23) / 2, 260, F("Please wait until done."));
  }
}
//rock_20211025 上电回零提示
static void Electricity_back_to_zero(const bool parking/*=false*/) {
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  DWIN_ICON_Show(ICON, ICON_BLTouch, 101, 105);
  if (HMI_IsChinese()) {    
     DWIN_ICON_Show(ICON, ICON_Power_On_Home_C, 14, 205);  //rock_20211025
  }
  else {
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * (parking ? 7 : 15)) / 2, 230, parking ? F("Parking") : F("Moving axis init..."));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * 23) / 2, 260, F("This operation is forbidden!"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * 23) / 2, 290, F("We need to go back to zero."));
  }
}
#if HAS_ONESTEP_LEVELING

  void Popup_Window_Leveling() 
  {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    DWIN_ICON_Show(ICON, ICON_AutoLeveling, 101, 105);
    if (HMI_IsChinese()) 
    {
      DWIN_Frame_AreaCopy(1, 0, 371, 100, 386, 84, 240);
      DWIN_Frame_AreaCopy(1, 0, 389, 150, 402, 61, 280);
    }
    else {
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 13) / 2, 230, GET_TEXT_F(MSG_BED_LEVELING));
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 23) / 2, 260, F("Please wait until done."));
    }
  }

#endif

void Draw_Select_Highlight(const bool sel) {
  HMI_flag.select_flag = sel;
  const uint16_t c1 = sel ? Select_Color : Color_Bg_Window,
                 c2 = sel ? Color_Bg_Window : Select_Color;
  DWIN_Draw_Rectangle(0, c1, 25, 279, 126, 318);
  DWIN_Draw_Rectangle(0, c1, 24, 278, 127, 319);
  DWIN_Draw_Rectangle(0, c2, 145, 279, 246, 318);
  DWIN_Draw_Rectangle(0, c2, 144, 278, 247, 319);
}

void Popup_window_PauseOrStop() {
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  if (HMI_IsChinese()) 
  {
    if(select_print.now == 1) DWIN_Frame_AreaCopy(1, 237, 338, 269, 356, 98, 150);
    else if (select_print.now == 2) DWIN_Frame_AreaCopy(1, 221, 320, 253, 336, 98, 150);
    DWIN_Frame_AreaCopy(1, 220, 304, 264, 319, 130, 150);
    DWIN_ICON_Show(ICON, ICON_Confirm_C, 26, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_C, 146, 280);
  }
  else 
  {
    if (select_print.now == 1) DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 11) / 2, 150, GET_TEXT_F(MSG_PAUSE_PRINT));
    else if (select_print.now == 2) DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 10) / 2, 150, GET_TEXT_F(MSG_STOP_PRINT));
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 26, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_E, 146, 280);
  }
  Draw_Select_Highlight(true);
}

void Draw_Printing_Screen() 
{
  if (HMI_IsChinese()) 
  {
    DWIN_Frame_AreaCopy(1, 30,  1,  71, 14,  14,   9);  // Tune
    DWIN_Frame_AreaCopy(1,  0, 72,  63, 86,  41, 188);  // Pause
    DWIN_Frame_AreaCopy(1, 65, 72, 128, 86, 176, 188);  // Stop
  }
  else 
  {
    // DWIN_Frame_AreaCopy(1, 40,  2,  92, 14,  14,   9);  // Tune  rock_20211123
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 14, 4, (char*)"Printing");
    DWIN_Frame_AreaCopy(1,  0, 44,  96, 58,  41, 188);  // 打印时间
    DWIN_Frame_AreaCopy(1, 98, 44, 152, 56, 176, 188);  // 剩余时间  rock_20211019
  }
}

void Draw_Print_ProgressBar() 
{
  DWIN_ICON_Show(ICON, ICON_Bar, 15, 98);
  DWIN_Draw_Rectangle(1, BarFill_Color, 16 + _card_percent * 240 / 100, 98, 256, 110); //rock_20210917
  DWIN_Draw_IntValue(true, true, 0, font8x16, Percent_Color, Color_Bg_Black, 3, 109, 133, _card_percent);
  DWIN_Draw_String(false, false, font8x16, Percent_Color, Color_Bg_Black, 133, 133, F("%"));
}

void Draw_Print_ProgressElapsed() 
{
  bool temp_flash_elapsed_time=false;
  static bool temp_elapsed_record_flag=true;
  duration_t elapsed = print_job_timer.duration(); // print timer
  if(elapsed.value<360000) //rock_20210903
  {
    temp_flash_elapsed_time=false;
    //如果剩余时间》100h，就填充黑色并刷新时间
    if(temp_elapsed_record_flag!=temp_flash_elapsed_time)
    {
      Clear_Print_Time();
    }
    DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 42, 212, elapsed.value / 3600);
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 58, 212, F(":"));
    DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 66, 212, (elapsed.value % 3600) / 60);
  }
  else 
  {
    temp_flash_elapsed_time=true;
    if(temp_elapsed_record_flag!=temp_flash_elapsed_time)
    {
      Clear_Print_Time();
    }
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 42, 212, F(">100H"));
  }
  temp_elapsed_record_flag=temp_flash_elapsed_time;
}

void Draw_Print_ProgressRemain() 
{
  bool temp_flash_remain_time=false;
  static bool temp_record_flag=true;
  if(HMI_flag.cloud_printing_flag)
  {
    Clear_Remain_Time();
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 176, 212, F("------"));
    return ;
  }
  if(_remain_time<360000) //rock_20210903
  {
    temp_flash_remain_time=false;
    if(temp_record_flag!=temp_flash_remain_time)
    {
      Clear_Remain_Time();
    }        
    DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 176, 212, _remain_time / 3600);
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 192, 212, F(":"));
    DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 200, 212, (_remain_time % 3600) / 60);
  }
  else 
  {
    temp_flash_remain_time=true;
    if(temp_record_flag!=temp_flash_remain_time)
    {
      Clear_Remain_Time();
    }
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 176, 212, F(">100H"));
  }  
  temp_record_flag=temp_flash_remain_time;  
}

void Popup_window_Filament(void)
{
  DWIN_Draw_Rectangle(1, Color_Bg_Blue,		0,	0,	272,	30);
  DWIN_Draw_Rectangle(1, Color_Bg_Black,	0,	31,	272,	360);
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 271-13, 330);
  if(HMI_IsChinese())//if(HMI_flag.language)//HMI_IsChinese()
  {
    DWIN_Frame_AreaCopy(1, 160, 338, 271-36, 479-125, 98, 100);
    DWIN_Frame_AreaCopy(1, 0, 286, 204, 302, 34, 160);
    DWIN_Frame_AreaCopy(1, 0, 303, 159, 319, 56, 180);
    DWIN_Frame_AreaCopy(1, 169, 303, 185, 318, 95, 200);
    DWIN_Frame_AreaCopy(1, 33, 321, 99, 335, 111, 200);
    DWIN_ICON_Show(ICON, ICON_Confirm_C, 26, 280);
    // DWIN_ICON_Show(ICON, ICON_NoTips_C, 146, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_C, 146, 280);
  }
  else
  {
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 120, 100, (char*)"Tips");
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 32, 150, (char*)"Filament has been used up,");
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 20, 170, (char*)"please replace the filament,");
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 20, 190, (char*)"click confirm after finishing");
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 28, 210, (char*)"replacement, or not prompt.");
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 26, 280);
    // DWIN_ICON_Show(ICON, ICON_NoTips_E, 146, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_E, 146, 280);
  }
  DWIN_Draw_Rectangle(0, Color_Bg_Window, 145, 279, 246, 318);
  DWIN_Draw_Rectangle(0, Color_Bg_Window, 144, 278, 247, 319);
  DWIN_Draw_Rectangle(0, Select_Color, 25, 279, 126, 318);
  DWIN_Draw_Rectangle(0, Select_Color, 24, 278, 127, 319);
}

void Popup_window_Remove_card(void)
{
  DWIN_Draw_Rectangle(1, Color_Bg_Blue,		0,	0,	272,	30);
  DWIN_Draw_Rectangle(1, Color_Bg_Black,	0,	31,	272,	360);
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 271-13, 330);
  if(HMI_IsChinese())//if(HMI_flag.language)
  {
    DWIN_ICON_Show(ICON, ICON_Card_Remove_C,14, 150);  //显示拔卡异常界面
    DWIN_Frame_AreaCopy(1, 160, 338, 271-36, 479-125, 98, 100);//溫馨提示
    DWIN_ICON_Show(ICON, ICON_Confirm_C, 86, 280);     //显示确认按钮
    //DWIN_ICON_Show(ICON, ICON_NoTips_C, 146, 280);
  }
  else
  {   
    DWIN_ICON_Show(ICON, ICON_Card_Remove_E,14, 150);  //显示拔卡异常界面
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Window, 120, 100, (char*)"Tips");  //rock_20210918
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
  }

  DWIN_Draw_Rectangle(0, Select_Color, 85, 279, 186, 318);
  DWIN_Draw_Rectangle(0, Select_Color, 84, 278, 187, 319);
}
void Goto_PrintProcess() 
{
  checkkey = PrintProcess;
  Clear_Main_Window();
  Draw_Printing_Screen();
  ICON_Tune();
  // if (printingIsPaused()&& !HMI_flag.cloud_printing_flag) ICON_Continue(); 
  if (printingIsPaused()) ICON_Continue(); 
  // else if(HMI_flag.cloud_printing_flag && HMI_flag.filement_resume_flag)ICON_Continue(); 
  else   ICON_Pause();
  ICON_Stop();
  // Copy into filebuf string before entry

  char * const name = card.longest_filename();
  const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(name) * MENU_CHR_W) / 2;
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, 60, name);

  DWIN_ICON_Show(ICON, ICON_PrintTime, 17, 193);
  DWIN_ICON_Show(ICON, ICON_RemainTime, 150, 191);
  
  _card_percent = card.percentDone();
  Draw_Print_ProgressBar();
  Draw_Print_ProgressElapsed();  //显示当前时间
  Draw_Print_ProgressRemain();   //显示剩余时间
}

void Goto_MainMenu() 
{
  checkkey = MainMenu;
  Clear_Main_Window();
  if (HMI_IsChinese()) 
  {
    DWIN_Frame_AreaCopy(1, 2, 2, 27, 14, 14, 9); // "Home"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MAIN));
    #else
      DWIN_Frame_AreaCopy(1, 0, 2, 39, 12, 14, 9);
    #endif
  }

  DWIN_ICON_Show(ICON, ICON_LOGO, 71, 52);
  ICON_Print();
  ICON_Prepare();
  ICON_Control();
  TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(select_page.now == 3);
}

inline ENCODER_DiffState get_encoder_state() {
  static millis_t Encoder_ms = 0;
  const millis_t ms = millis();
  if (PENDING(ms, Encoder_ms)) return ENCODER_DIFF_NO;
  const ENCODER_DiffState state = Encoder_ReceiveAnalyze();
  if (state != ENCODER_DIFF_NO) Encoder_ms = ms + ENCODER_WAIT_MS;
  return state;
}

void HMI_Plan_Move(const feedRate_t fr_mm_s) {
  if (!planner.is_full()) {
    planner.synchronize();
    planner.buffer_line(current_position, fr_mm_s, active_extruder);
    DWIN_UpdateLCD();
  }
}
void HMI_Move_Done(const AxisEnum axis) 
{
  // EncoderRate.enabled = false;  //rock_20211120    
  planner.synchronize();
  checkkey = AxisMove;
  DWIN_UpdateLCD();
}

void HMI_Move_X() 
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_X_scaled))
    {
      DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
      return HMI_Move_Done(X_AXIS);
    }      
    LIMIT(HMI_ValueStruct.Move_X_scaled, (X_MIN_POS) * MINUNITMULT, (X_MAX_POS) * MINUNITMULT);
    current_position.x = HMI_ValueStruct.Move_X_scaled / MINUNITMULT;
    DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, 216,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
    DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(X_AXIS));
  }
}


void HMI_Move_Y() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Y_scaled))
    {
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        return HMI_Move_Done(Y_AXIS);
    }
      
    LIMIT(HMI_ValueStruct.Move_Y_scaled, (Y_MIN_POS) * MINUNITMULT, (Y_MAX_POS) * MINUNITMULT);
    current_position.y = HMI_ValueStruct.Move_Y_scaled / MINUNITMULT;
    DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);  
    DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(Y_AXIS));
  }
}


void HMI_Move_Z() 
{  
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();   
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);   
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Z_scaled))
    {
      DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
      return HMI_Move_Done(Z_AXIS);
    }      
    LIMIT(HMI_ValueStruct.Move_Z_scaled, (Z_AXIS_OFFSET_RANGE_MIN) * MINUNITMULT, (Z_MAX_POS) * MINUNITMULT);   //rock_20211025 修改轴移动界面不能移动到负值，防止撞击
    current_position.z = HMI_ValueStruct.Move_Z_scaled / MINUNITMULT;
   
    DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
    DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(Z_AXIS));
  }
}

#if HAS_HOTEND

  void HMI_Move_E() {
    static float last_E_scaled = 0;
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) 
    {
     
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_E_scaled))
      {
        last_E_scaled = HMI_ValueStruct.Move_E_scaled;
         DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        return HMI_Move_Done(E_AXIS);
      }
      LIMIT(HMI_ValueStruct.Move_E_scaled, last_E_scaled - (EXTRUDE_MAXLENGTH_e) * MINUNITMULT, last_E_scaled + (EXTRUDE_MAXLENGTH_e) * MINUNITMULT);
      current_position.e = HMI_ValueStruct.Move_E_scaled / MINUNITMULT;
     
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(4), HMI_ValueStruct.Move_E_scaled);
      DWIN_UpdateLCD();
      HMI_Plan_Move(MMM_TO_MMS(FEEDRATE_E));
    }
  }

#endif

#if HAS_ZOFFSET_ITEM

  bool printer_busy() { return planner.movesplanned() || printingIsActive(); }

  void HMI_Zoffset() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) 
    {
      uint8_t zoff_line;
      switch (HMI_ValueStruct.show_mode) {
        case -4: zoff_line = PREPARE_CASE_ZOFF + MROWS - index_prepare; break;
        default: zoff_line = TUNE_CASE_ZOFF + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.offset_value))
      {
        LIMIT(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MIN) * 100, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
        last_zoffset = dwin_zoffset;
        dwin_zoffset = HMI_ValueStruct.offset_value / 100.0f;  //rock_20210726 
        EncoderRate.enabled = false;
        #if HAS_BED_PROBE
          probe.offset.z = dwin_zoffset;
          TERN_(EEPROM_SETTINGS, settings.save());
        #endif
        checkkey = HMI_ValueStruct.show_mode == -4 ? Prepare : Tune;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(zoff_line), TERN(HAS_BED_PROBE, BABY_Z_VAR * 100, HMI_ValueStruct.offset_value));
        DWIN_UpdateLCD();
        return;
      }
      LIMIT(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MIN) * 100, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
      last_zoffset = dwin_zoffset;
      dwin_zoffset = HMI_ValueStruct.offset_value / 100.0f;
      #if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
        //if (BABYSTEP_ALLOWED()) babystep.add_mm(Z_AXIS, dwin_zoffset - last_zoffset);
        serialprintPGM("d:babystep\n");
        babystep.add_mm(Z_AXIS, dwin_zoffset - last_zoffset);
      #endif
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, 202, MBASE(zoff_line), HMI_ValueStruct.offset_value);
      DWIN_UpdateLCD();
    }
  }

#endif // HAS_ZOFFSET_ITEM

#if HAS_HOTEND

  void HMI_ETemp() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t temp_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: temp_line = TEMP_CASE_TEMP; break;
        case -2: temp_line = PREHEAT_CASE_TEMP; break;
        case -3: temp_line = PREHEAT_CASE_TEMP; break;
        default: temp_line = TUNE_CASE_TEMP + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.E_Temp)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), ui.material_preset[0].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), ui.material_preset[1].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1) // Temperature
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), HMI_ValueStruct.E_Temp);
        thermalManager.setTargetHotend(HMI_ValueStruct.E_Temp, 0);
        return;
      }
      // E_Temp limit
      LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
      // E_Temp value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(temp_line), HMI_ValueStruct.E_Temp);
    }
  }

#endif // HAS_HOTEND

#if HAS_HEATED_BED

  void HMI_BedTemp() 
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t bed_line;
      switch (HMI_ValueStruct.show_mode) 
      {
        case -1: bed_line = TEMP_CASE_BED; break;
        case -2: bed_line = PREHEAT_CASE_BED; break;
        case -3: bed_line = PREHEAT_CASE_BED; break;
        default: bed_line = TUNE_CASE_BED + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Bed_Temp)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), ui.material_preset[0].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), ui.material_preset[1].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), HMI_ValueStruct.Bed_Temp);
        thermalManager.setTargetBed(HMI_ValueStruct.Bed_Temp);
        return;
      }
      // Bed_Temp limit
      LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
      // Bed_Temp value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(bed_line), HMI_ValueStruct.Bed_Temp);
    }
  }

#endif // HAS_HEATED_BED

#if HAS_PREHEAT && HAS_FAN

  void HMI_FanSpeed() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t fan_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: fan_line = TEMP_CASE_FAN; break;
        case -2: fan_line = PREHEAT_CASE_FAN; break;
        case -3: fan_line = PREHEAT_CASE_FAN; break;
        default: fan_line = TUNE_CASE_FAN + MROWS - index_tune;
      }

      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Fan_speed)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), ui.material_preset[0].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), ui.material_preset[1].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), HMI_ValueStruct.Fan_speed);
        thermalManager.set_fan_speed(0, HMI_ValueStruct.Fan_speed);
        return;
      }
      // Fan_speed limit
      LIMIT(HMI_ValueStruct.Fan_speed, 0, 255);
      // Fan_speed value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(fan_line), HMI_ValueStruct.Fan_speed);
    }
  }

#endif // HAS_PREHEAT && HAS_FAN

void HMI_PrintSpeed() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.print_speed)) {
      checkkey = Tune;
      EncoderRate.enabled = false;
      feedrate_percentage = HMI_ValueStruct.print_speed;
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(select_tune.now + MROWS - index_tune), HMI_ValueStruct.print_speed);
      return;
    }
    // print_speed limit
    LIMIT(HMI_ValueStruct.print_speed, MIN_PRINT_SPEED, MAX_PRINT_SPEED);
    // print_speed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(select_tune.now + MROWS - index_tune), HMI_ValueStruct.print_speed);
  }
}

#define LAST_AXIS TERN(HAS_HOTEND, E_AXIS, Z_AXIS)

void HMI_MaxFeedspeedXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Feedspeed)) {
      checkkey = MaxSpeed;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, LAST_AXIS))
        planner.set_max_feedrate(HMI_flag.feedspeed_axis, HMI_ValueStruct.Max_Feedspeed);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
      return;
    }
    // MaxFeedspeed limit
    if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Feedspeed, default_max_feedrate[HMI_flag.feedspeed_axis] * 2);
    if (HMI_ValueStruct.Max_Feedspeed < MIN_MAXFEEDSPEED) HMI_ValueStruct.Max_Feedspeed = MIN_MAXFEEDSPEED;
    // MaxFeedspeed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
  }
}

void HMI_MaxAccelerationXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Acceleration)) {
      checkkey = MaxAcceleration;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.acc_axis, X_AXIS, LAST_AXIS))
        planner.set_max_acceleration(HMI_flag.acc_axis, HMI_ValueStruct.Max_Acceleration);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
      return;
    }
    // MaxAcceleration limit
    if (WITHIN(HMI_flag.acc_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Acceleration, default_max_acceleration[HMI_flag.acc_axis] * 2);
    if (HMI_ValueStruct.Max_Acceleration < MIN_MAXACCELERATION) HMI_ValueStruct.Max_Acceleration = MIN_MAXACCELERATION;
    // MaxAcceleration value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
  }
}

#if HAS_CLASSIC_JERK

  void HMI_MaxJerkXYZE() 
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Jerk_scaled)) 
      {
        checkkey = MaxJerk;
        EncoderRate.enabled = false;
        if (WITHIN(HMI_flag.jerk_axis, X_AXIS, LAST_AXIS))
        {
          planner.set_max_jerk(HMI_flag.jerk_axis, HMI_ValueStruct.Max_Jerk_scaled / 10);
          planner.max_jerk[HMI_flag.jerk_axis] = HMI_ValueStruct.Max_Jerk_scaled / 10;
        }

        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk_scaled);
        return;
      }
      // MaxJerk limit
      if (WITHIN(HMI_flag.jerk_axis, X_AXIS, LAST_AXIS))
        NOMORE(HMI_ValueStruct.Max_Jerk_scaled, default_max_jerk[HMI_flag.jerk_axis] * 2 * MINUNITMULT);
      NOLESS(HMI_ValueStruct.Max_Jerk_scaled, (MIN_MAXJERK) * MINUNITMULT);
      // MaxJerk value
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk_scaled);
    }
  }

#endif // HAS_CLASSIC_JERK

void HMI_StepXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Step_scaled)) {
      checkkey = Step;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.step_axis, X_AXIS, LAST_AXIS))
        planner.settings.axis_steps_per_mm[HMI_flag.step_axis] = HMI_ValueStruct.Max_Step_scaled / 10;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step_scaled);
      return;
    }
    // Step limit
    if (WITHIN(HMI_flag.step_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Step_scaled, 999.9 * MINUNITMULT);
    NOLESS(HMI_ValueStruct.Max_Step_scaled, MIN_STEP);
    // Step value
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step_scaled);
  }
}

// Draw X, Y, Z and blink if in an un-homed or un-trusted state
void _update_axis_value(const AxisEnum axis, const uint16_t x, const uint16_t y, const bool blink, const bool force) {
  const bool draw_qmark = axis_should_home(axis),
             draw_empty = NONE(HOME_AFTER_DEACTIVATE, DISABLE_REDUCED_ACCURACY_WARNING) && !draw_qmark && !axis_is_trusted(axis);

  // Check for a position change
  static xyz_pos_t oldpos = { -1, -1, -1 };
  const float p = current_position[axis];
  const bool changed = oldpos[axis] != p;
  if (changed) oldpos[axis] = p;

  if (force || changed || draw_qmark || draw_empty) {
    if (blink && draw_qmark)
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, x, y, F("???.?"));
    else if (blink && draw_empty)
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, x, y, F("     "));
    else
    // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, x, y, p * 10);
    // DWIN_Draw_Signed_Float(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value)
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, x, y, p * 10);
  }
}

void _draw_xyz_position(const bool force) {
  //SERIAL_ECHOPGM("Draw XYZ:");
  static bool _blink = false;
  const bool blink = !!(millis() & 0x400UL);
  if (force || blink != _blink) {
    _blink = blink;
    //SERIAL_ECHOPGM(" (blink)");
    _update_axis_value(X_AXIS,  35, 459, blink, true);
    _update_axis_value(Y_AXIS, 120, 459, blink, true);
    _update_axis_value(Z_AXIS, 205, 459, blink, true);
  }
  //SERIAL_EOL();
}
//取绝对值
static int8_t get_absolute_Value(int8_t value)
{
  if(value<0)value=(-value);
  return value;
}
//获取当前补偿值
static int8_t Get_Compensation_value(int8_t big_value,int8_t small_value,uint8_t level,celsius_t Current_Value)
{
  int8_t difference_value=get_absolute_Value(big_value)-get_absolute_Value(small_value);
  uint8_t proportion_value=0; //
  int8_t result_value=0;
  if(Current_Value<Tem_standard_10)result_value=big_value;
  else if(Current_Value>Tem_standard_50)result_value=small_value;
  else {
    switch(level)
    {
      case 1:
        proportion_value=(difference_value/(Tem_standard_25-Tem_standard_10))*(Tem_standard_25-Current_Value);
      break;
      case 2:
      proportion_value=(difference_value/(Tem_standard_35-Tem_standard_25))*(Tem_standard_35-Current_Value);
      break;
      case 3:
      proportion_value=(difference_value/(Tem_standard_40-Tem_standard_35))*(Tem_standard_40-Current_Value);
      break;
      case 4:
      proportion_value=(difference_value/(Tem_standard_50-Tem_standard_40))*(Tem_standard_50-Current_Value);
      break;
      default:
      proportion_value=0;
      break;
    }
    if(big_value<0) result_value=big_value+proportion_value;
    else            result_value=big_value-proportion_value;
  }
  return result_value;
}

//温度补偿函数
static celsius_t Temp_Compensation_Func(uint8_t Type,celsius_t Temperature_Value)
{
  uint8_t compensation_level;
  celsius_t temp_value=Temperature_Value;
  if(Temperature_Value>=Tem_compens_value_10 && Temperature_Value<=Tem_compens_value_25)
    compensation_level=1; 
  else if(Temperature_Value>=Tem_compens_value_25 && Temperature_Value<=Tem_compens_value_35)
    compensation_level=2; 
  else if(Temperature_Value>=Tem_compens_value_35 && Temperature_Value<=Tem_compens_value_40)
    compensation_level=3; 
  else if(Temperature_Value>=Tem_compens_value_40 && Temperature_Value<=Tem_compens_value_50)
    compensation_level=4; 
  else  compensation_level=0;
  switch(Type)
  {
    case NOZZLE:
      switch(compensation_level)
      {
        case 1:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_nozzle[0],demarcate_data.demarcate_set_nozzle[1],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_nozzle[0];
        break;
        case 2:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_nozzle[1],demarcate_data.demarcate_set_nozzle[2],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_nozzle[1];
        break;
        case 3:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_nozzle[2],demarcate_data.demarcate_set_nozzle[3],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_nozzle[2]; 
        break;
        case 4:
          temp_value=Temperature_Value+ demarcate_data.demarcate_set_nozzle[3];//Get_Compensation_value(demarcate_data.demarcate_set_nozzle[3],demarcate_data.demarcate_set_nozzle[4],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_nozzle[3];
        break; 
        default:
          temp_value=Temperature_Value;
        break;   
      }
    break;
    case BED:
      switch(compensation_level)
      {
        case 1:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_bed[0],demarcate_data.demarcate_set_bed[1],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_bed[0];
        break;
        case 2:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_bed[1],demarcate_data.demarcate_set_bed[2],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_bed[1];
        break;
        case 3:
          temp_value=Temperature_Value+ Get_Compensation_value(demarcate_data.demarcate_set_bed[2],demarcate_data.demarcate_set_bed[3],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_bed[2];
        break;
        case 4:
          temp_value=Temperature_Value+ demarcate_data.demarcate_set_bed[3];//Get_Compensation_value(demarcate_data.demarcate_set_bed[3],demarcate_data.demarcate_set_bed[4],compensation_level,Temperature_Value);//demarcate_data.demarcate_set_bed[3];
        break;
        default:
          temp_value=Temperature_Value;
        break;      
      }
    break;
  }
  return temp_value;
}

void update_variable() 
{
  #if HAS_HOTEND
    static celsius_t _hotendtemp = 0, _hotendtarget = 0;
    const celsius_t hc = thermalManager.wholeDegHotend(0),
                    ht = thermalManager.degTargetHotend(0);
    const bool _new_hotend_temp = _hotendtemp != hc,
               _new_hotend_target = _hotendtarget != ht;
    if (_new_hotend_temp) _hotendtemp = hc;
    if (_new_hotend_target) _hotendtarget = ht;
  #endif
  #if HAS_HEATED_BED
    static celsius_t _bedtemp = 0, _bedtarget = 0;
    const celsius_t bc = thermalManager.wholeDegBed(),
                    bt = thermalManager.degTargetBed();
    const bool _new_bed_temp = _bedtemp != bc,
               _new_bed_target = _bedtarget != bt;
    if (_new_bed_temp) _bedtemp = bc;
    if (_new_bed_target) _bedtarget = bt;
  #endif
  #if HAS_FAN
    static uint8_t _fanspeed = 0;
    const bool _new_fanspeed = _fanspeed != thermalManager.fan_speed[0];
    if (_new_fanspeed) _fanspeed = thermalManager.fan_speed[0];
  #endif

  if (checkkey == Tune) 
  {
    // Tune page temperature update
    #if HAS_HOTEND
      if (_new_hotend_target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_TEMP + MROWS - index_tune), _hotendtarget);
    #endif
    #if HAS_HEATED_BED
      if (_new_bed_target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_BED + MROWS - index_tune), _bedtarget);
    #endif
    #if HAS_FAN
      if (_new_fanspeed)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_FAN + MROWS - index_tune), _fanspeed);
    #endif
  }
  else if (checkkey == TemperatureID) {
    // Temperature page temperature update
    #if HAS_HOTEND
      if (_new_hotend_target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_TEMP), _hotendtarget);
    #endif
    #if HAS_HEATED_BED
      if (_new_bed_target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_BED), _bedtarget);
    #endif
    #if HAS_FAN
      if (_new_fanspeed)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_FAN), _fanspeed);
    #endif
  }

  // Bottom temperature update

  #if HAS_HOTEND
    if (_new_hotend_temp)
    {      
      if(HMI_flag.demarcate_flag)
      {
        SERIAL_ECHOLNPAIR("_hotendtemp_1=: ", _hotendtemp);
        _hotendtemp=Temp_Compensation_Func(NOZZLE,_hotendtemp);  //rock_20211012
        SERIAL_ECHOLNPAIR("_hotendtemp_2=: ", _hotendtemp);
      }      
      DWIN_Draw_Signed_Float(DWIN_FONT_STAT,Color_Bg_Black, 3, 0, 32, 384, _hotendtemp);  //rock_20210820
    }
      //DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 28, 384, _hotendtemp);
      
    if (_new_hotend_target)
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 384, _hotendtarget);

    static int16_t _flow = planner.flow_percentage[0];
    if (_flow != planner.flow_percentage[0]) {
      _flow = planner.flow_percentage[0];
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 417, _flow);
    }
  #endif

  #if HAS_HEATED_BED
    if (_new_bed_temp)
    {
      
      if(HMI_flag.demarcate_flag)
      {
        SERIAL_ECHOLNPAIR("_bedtemp_1=: ", _bedtemp);
       _bedtemp=Temp_Compensation_Func(BED,_bedtemp);  //rock_20211012
       SERIAL_ECHOLNPAIR("_bedtemp_2=: ", _bedtemp);
      }      
      DWIN_Draw_Signed_Float(DWIN_FONT_STAT,Color_Bg_Black, 3, 0, 32, 417, _bedtemp);  //rock_20210820
    }
    
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 28, 417, _bedtemp);
    if (_new_bed_target)
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 417, _bedtarget);
  #endif

  static int16_t _feedrate = 100;
  if (_feedrate != feedrate_percentage) {
    _feedrate = feedrate_percentage;
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 384, _feedrate);
  }

  #if HAS_FAN
    if (_new_fanspeed) {
      _fanspeed = thermalManager.fan_speed[0];
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 195 + 2 * STAT_CHR_W, 384, _fanspeed);
    }
  #endif

  static float _offset = 0;
  if (BABY_Z_VAR != _offset) {
    _offset = BABY_Z_VAR;
    if (BABY_Z_VAR < 0) {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 207, 417, -_offset * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 205, 419, F("-"));
    }
    else {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 207, 417, _offset * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 205, 419, F(" "));
    }
  }

  _draw_xyz_position(false);
}

/**
 * Read and cache the working directory.
 *
 * TODO: New code can follow the pattern of menu_media.cpp
 * and rely on Marlin caching for performance. No need to
 * cache files here.
 */

#ifndef strcasecmp_P
  #define strcasecmp_P(a, b) strcasecmp((a), (b))
#endif

void make_name_without_ext(char *dst, char *src, size_t maxlen=MENU_CHAR_LIMIT) {
  char * const name = card.longest_filename();
  size_t pos        = strlen(name); // index of ending nul

  // For files, remove the extension
  // which may be .gcode, .gco, or .g
  if (!card.flag.filenameIsDir)
    while (pos && src[pos] != '.') pos--; // find last '.' (stop at 0)

  size_t len = pos;   // nul or '.'
  if (len > maxlen) {           // Keep the name short
    pos        = len = maxlen; // move nul down
    dst[--pos] = '.';           // insert dots
    dst[--pos] = '.';
    dst[--pos] = '.';
  }

  dst[len] = '\0';    // end it

  // Copy down to 0
  while (pos--) dst[pos] = src[pos];
}

void HMI_SDCardInit() { card.cdroot(); }

void MarlinUI::refresh() { /* Nothing to see here */ }

#define ICON_Folder ICON_More

#if ENABLED(SCROLL_LONG_FILENAMES)

  char shift_name[LONG_FILENAME_LENGTH + 1];
  int8_t shift_amt; // = 0
  millis_t shift_ms; // = 0

  // Init the shift name based on the highlighted item
  void Init_Shift_Name() {
    const bool is_subdir = !card.flag.workDirIsRoot;
    const int8_t filenum = select_file.now - 1 - is_subdir; // Skip "Back" and ".."
    const uint16_t fileCnt = card.get_num_Files();
    if (WITHIN(filenum, 0, fileCnt - 1)) {
      card.getfilename_sorted(SD_ORDER(filenum, fileCnt));
      char * const name = card.longest_filename();
      make_name_without_ext(shift_name, name, 100);
    }
  }

  void Init_SDItem_Shift() {
    shift_amt = 0;
    shift_ms  = select_file.now > 0 && strlen(shift_name) > MENU_CHAR_LIMIT
           ? millis() + 750UL : 0;
  }

#endif

/**
 * Display an SD item, adding a CDUP for subfolders.
 */
void Draw_SDItem(const uint16_t item, int16_t row=-1) {
  if (row < 0) row = item + 1 + MROWS - index_file;
  const bool is_subdir = !card.flag.workDirIsRoot;
  if (is_subdir && item == 0) {
    Draw_Menu_Line(row, ICON_Folder, "..");
    return;
  }

  card.getfilename_sorted(SD_ORDER(item - is_subdir, card.get_num_Files()));
  char * const name = card.longest_filename();

  #if ENABLED(SCROLL_LONG_FILENAMES)
    // Init the current selected name
    // This is used during scroll drawing
    if (item == select_file.now - 1) {
      make_name_without_ext(shift_name, name, 100);
      Init_SDItem_Shift();
    }
  #endif

  // Draw the file/folder with name aligned left
  char str[strlen(name) + 1];
  make_name_without_ext(str, name);
  Draw_Menu_Line(row, card.flag.filenameIsDir ? ICON_Folder : ICON_File, str);
}

#if ENABLED(SCROLL_LONG_FILENAMES)

  void Draw_SDItem_Shifted(uint8_t &shift) {
    // Limit to the number of chars past the cutoff
    const size_t len = strlen(shift_name);
    NOMORE(shift, _MAX(len - MENU_CHAR_LIMIT, 0U));

    // Shorten to the available space
    const size_t lastchar = _MIN((signed)len, shift + MENU_CHAR_LIMIT);

    const char c = shift_name[lastchar];
    shift_name[lastchar] = '\0';

    const uint8_t row = select_file.now + MROWS - index_file; // skip "Back" and scroll
    Erase_Menu_Text(row);
    Draw_Menu_Line(row, 0, &shift_name[shift]);

    shift_name[lastchar] = c;
  }

#endif

// Redraw the first set of SD Files
void Redraw_SD_List() {
  select_file.reset();
  index_file = MROWS;

  Clear_Menu_Area(); // Leave title bar unchanged

  Draw_Back_First();

  if (card.isMounted()) {
    // As many files as will fit
    LOOP_L_N(i, _MIN(nr_sd_menu_items(), MROWS))
      Draw_SDItem(i, i + 1);

    TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
  }
  // else {
  //   DWIN_Draw_Rectangle(1, Color_Bg_Red, 10, MBASE(3) - 10, DWIN_WIDTH - 10, MBASE(4));
  //   DWIN_Draw_String(false, false, font16x32, Color_Yellow, Color_Bg_Red, ((DWIN_WIDTH) - 8 * 16) / 2, MBASE(3), F("No Media"));
  // }
}

bool DWIN_lcd_sd_status = false;
bool pause_action_flag = false;

void SDCard_Up() {
  card.cdup();
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

void SDCard_Folder(char * const dirname) {
  card.cd(dirname);
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

//
// Watch for media mount / unmount
//
void HMI_SDCardUpdate() {
  if (HMI_flag.home_flag||HMI_flag.disallow_recovery_flag) return;  //rock_20211020 在界面回零过程中不检测拔卡动作。 增加||HMI_flag.disallow_recovery_flag
  if (DWIN_lcd_sd_status != card.isMounted()) {
    DWIN_lcd_sd_status = card.isMounted();
    //SERIAL_ECHOLNPAIR("HMI_SDCardUpdate: ", DWIN_lcd_sd_status);
    if (DWIN_lcd_sd_status) {
      if (checkkey == SelectFile)
        Redraw_SD_List();
    }
    else {
      // clean file icon
      if (checkkey == SelectFile) 
      {
        Redraw_SD_List();
      }
      else if (checkkey == PrintProcess || checkkey == Tune || printingIsActive())
      {
        /*
        if()
        {
            // TODO: Move card removed abort handling
          //       to CardReader::manage_media.
          card.abortFilePrintSoon();
          wait_for_heatup = wait_for_user = false;
          dwin_abort_flag = true; // Reset feedrate, return to Home
        }
        */
       
      }
    }
    DWIN_UpdateLCD();
  }
}

//
// The status area is always on-screen, except during
// full-screen modal dialogs. (TODO: Keep alive during dialogs)
//
void Draw_Status_Area(const bool with_update) {

  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, STATUS_Y, DWIN_WIDTH, DWIN_HEIGHT - 1);

  #if HAS_HOTEND
    DWIN_ICON_Show(ICON, ICON_HotendTemp, 10, 383);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 28, 384, thermalManager.wholeDegHotend(0));
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 25 + 3 * STAT_CHR_W + 5, 384, F("/"));
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 384, thermalManager.degTargetHotend(0)); 

    DWIN_ICON_Show(ICON, ICON_StepE, 112, 417);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 417, planner.flow_percentage[0]);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 417, F("%"));
  #endif

  #if HAS_HEATED_BED
    DWIN_ICON_Show(ICON, ICON_BedTemp, 10, 416);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 28, 417, thermalManager.wholeDegBed());
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 25 + 3 * STAT_CHR_W + 5, 417, F("/"));
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 417, thermalManager.degTargetBed());
  #endif

  DWIN_ICON_Show(ICON, ICON_Speed, 113, 383);
  DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 384, feedrate_percentage);
  DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 384, F("%"));

  #if HAS_FAN
    DWIN_ICON_Show(ICON, ICON_FanSpeed, 187, 383);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 195 + 2 * STAT_CHR_W, 384, thermalManager.fan_speed[0]);
  #endif

  #if HAS_ZOFFSET_ITEM
    DWIN_ICON_Show(ICON, ICON_Zoffset, 187, 416);
  #endif

  if (BABY_Z_VAR < 0) {
    DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 207, 417, -BABY_Z_VAR * 100);
    DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 205, 419, F("-"));
  }
  else {
    DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 207, 417, BABY_Z_VAR * 100);
    DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 205, 419, F(" "));
  }

  DWIN_Draw_Rectangle(1, Line_Color, 0, 449, DWIN_WIDTH, 451);

  DWIN_ICON_Show(ICON, ICON_MaxSpeedX,  10, 456);
  DWIN_ICON_Show(ICON, ICON_MaxSpeedY,  95, 456);
  DWIN_ICON_Show(ICON, ICON_MaxSpeedZ, 180, 456);
  _draw_xyz_position(true);

  if (with_update) {
    DWIN_UpdateLCD();
    delay(5);
  }
}

void HMI_StartFrame(const bool with_update) {
  Goto_MainMenu();
  Draw_Status_Area(with_update);
}

void Draw_Info_Menu() {
  Clear_Main_Window();

  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(MACHINE_SIZE) * MENU_CHR_W) / 2, 122, F(MACHINE_SIZE));
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(SHORT_BUILD_VERSION) * MENU_CHR_W) / 2, 195, F(SHORT_BUILD_VERSION));

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 30, 17, 57, 29); // "Info"

    DWIN_Frame_AreaCopy(1, 197, 149, 252, 161, 108, 102);
    DWIN_Frame_AreaCopy(1, 1, 164, 56, 176, 108, 175);
    DWIN_Frame_AreaCopy(1, 58, 164, 113, 176, 105, 248);  //
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_C) * MENU_CHR_W) / 2, 268, F(CORP_WEBSITE_C));

  }

  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_INFO_SCREEN));
    #else
      DWIN_Frame_TitleCopy(1, 190, 16, 215, 26); // "Info"
    #endif

    DWIN_Frame_AreaCopy(1, 120, 150, 146, 161, 124, 102);
    DWIN_Frame_AreaCopy(1, 146, 151, 254, 161, 82, 175);
    DWIN_Frame_AreaCopy(1, 0, 165, 94, 175, 89, 248);
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_E) * MENU_CHR_W) / 2, 268, F(CORP_WEBSITE_E));

  }
  
  Draw_Back_First();
  LOOP_L_N(i, 3) {
    DWIN_ICON_Show(ICON, ICON_PrintSize + i, 26, 99 + i * 73);
    DWIN_Draw_Line(Line_Color, 16, MBASE(2) + i * 73, 256, 156 + i * 73);
  }
}

void Draw_Print_File_Menu() {
  Clear_Title_Bar();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 0, 31, 55, 44); // "Print file"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Print file"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 52, 31, 137, 41); // "Print file"
    #endif
  }

  Redraw_SD_List();
}

/* Main Process */
void HMI_MainMenu() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_page.inc(4)) {
      switch (select_page.now) {
        case 0: ICON_Print(); break;
        case 1: ICON_Print(); ICON_Prepare(); break;
        case 2: ICON_Prepare(); ICON_Control(); break;
        case 3: ICON_Control(); TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(1); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_page.dec()) {
      switch (select_page.now) 
      {
        case 0: ICON_Print(); ICON_Prepare(); break;
        case 1: ICON_Prepare(); ICON_Control(); break;
        case 2: ICON_Control(); TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(0); break;
        case 3: TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(1); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_page.now) {
      case 0: // Print File
        checkkey = SelectFile;
        Draw_Print_File_Menu();
        break;

      case 1: // Prepare
        checkkey = Prepare;
        select_prepare.reset();
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;

      case 2: // Control
        checkkey = Control;
        select_control.reset();
        index_control = MROWS;
        Draw_Control_Menu();
        break;

      case 3: // Leveling or Info
        #if HAS_ONESTEP_LEVELING
          checkkey = Leveling;
          HMI_Leveling();
        #else
          checkkey = Info;
          Draw_Info_Menu();
        #endif
        break;
    }
  }
  DWIN_UpdateLCD();
}

// Select (and Print) File
void HMI_SelectFile() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();

  const uint16_t hasUpDir = !card.flag.workDirIsRoot;

  if (encoder_diffState == ENCODER_DIFF_NO) {
    #if ENABLED(SCROLL_LONG_FILENAMES)
      if (shift_ms && select_file.now >= 1 + hasUpDir) {
        // Scroll selected filename every second
        const millis_t ms = millis();
        if (ELAPSED(ms, shift_ms)) {
          const bool was_reset = shift_amt < 0;
          shift_ms = ms + 375UL + was_reset * 250UL;  // ms per character
          uint8_t shift_new = shift_amt + 1;           // Try to shift by...
          Draw_SDItem_Shifted(shift_new);             // Draw the item
          if (!was_reset && shift_new == 0)           // Was it limited to 0?
            shift_ms = 0;                             // No scrolling needed
          else if (shift_new == shift_amt)            // Scroll reached the end
            shift_new = -1;                           // Reset
          shift_amt = shift_new;                      // Set new scroll
        }
      }
    #endif
    return;
  }

  // First pause is long. Easy.
  // On reset, long pause must be after 0.

  const uint16_t fullCnt = nr_sd_menu_items();

  if (encoder_diffState == ENCODER_DIFF_CW && fullCnt) {
    if (select_file.inc(1 + fullCnt)) {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms)) {             // If line was shifted
        Erase_Menu_Text(itemnum + MROWS - index_file);          // Erase and
        Draw_SDItem(itemnum - 1);                               // redraw
      }
      if (select_file.now > MROWS && select_file.now > index_file) { // Cursor past the bottom
        index_file = select_file.now;                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_SDItem(itemnum, MROWS);                            // Draw and init the shift name
      }
      else {
        Move_Highlight(1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());         // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && fullCnt) {
    if (select_file.dec()) {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms)) {             // If line was shifted
        Erase_Menu_Text(select_file.now + 1 + MROWS - index_file); // Erase and
        Draw_SDItem(itemnum + 1);                               // redraw
      }
      if (select_file.now < index_file - MROWS) {               // Cursor past the top
        index_file--;                                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_DOWN);
        if (index_file == MROWS) {
          Draw_Back_First();
          TERN_(SCROLL_LONG_FILENAMES, shift_ms = 0);
        }
        else {
          Draw_SDItem(itemnum, 0);                              // Draw the item (and init shift name)
        }
      }
      else {
        Move_Highlight(-1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());        // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());        // Reset left. Init timer.
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (select_file.now == 0) { // Back
      select_page.set(0);
      Goto_MainMenu();
    }
    else if (hasUpDir && select_file.now == 1) { // CD-Up
      SDCard_Up();
      goto HMI_SelectFileExit;
    }
    else {
      const uint16_t filenum = select_file.now - 1 - hasUpDir;
      card.getfilename_sorted(SD_ORDER(filenum, card.get_num_Files()));

      // Enter that folder!
      if (card.flag.filenameIsDir) {
        SDCard_Folder(card.filename);
        goto HMI_SelectFileExit;
      }

      // Reset highlight for next entry
      select_print.reset();
      select_file.reset();

      // Start choice and print SD file
      HMI_flag.heat_flag = true;
      HMI_flag.print_finish = false;
      HMI_ValueStruct.show_mode = 0;
      recovery.info.sd_printing_flag=true; //rock_20210819
      //SERIAL_ECHOLNPAIR("\r\n sd_printing_flag: ", recovery.info.sd_printing_flag);//rock 20210730
      card.openAndPrintFile(card.filename);

      #if HAS_FAN
        // All fans on for Ender 3 v2 ?
        // The slicer should manage this for us.
        //for (uint8_t i = 0; i < FAN_COUNT; i++)
        //  thermalManager.fan_speed[i] = 255;
      #endif
      //print_job_timer.reset();  //rock_20210726 打印时间清
      Goto_PrintProcess();
    }
  }
HMI_SelectFileExit:
  DWIN_UpdateLCD();
}

/* Printing */
void HMI_Printing() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (HMI_flag.done_confirm_flag) 
  {
    if (encoder_diffState == ENCODER_DIFF_ENTER) {
      HMI_flag.done_confirm_flag = false;
      dwin_abort_flag = true; // Reset feedrate, return to Home
    }
    return;
  }

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_print.inc(3)) {
      switch (select_print.now) 
      {
        case 0: ICON_Tune(); break;
        case 1:
          ICON_Tune();
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          break;
        case 2:
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          ICON_Stop();
          break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) 
  {
    if (select_print.dec()) {
      switch (select_print.now) {
        case 0:
          ICON_Tune();
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          break;
        case 1:
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          ICON_Stop();
          break;
        case 2: ICON_Stop(); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_print.now) 
    {
      case 0: // Tune
        checkkey = Tune;
        HMI_ValueStruct.show_mode = 0;
        select_tune.reset();
        index_tune = MROWS;
        Draw_Tune_Menu();
        break;
      case 1: // Pause
        if (HMI_flag.pause_flag) 
        { //确定 
          ICON_Pause();
          char cmd[40];
          cmd[0] = '\0';
          #if BOTH(HAS_HEATED_BED, PAUSE_HEAT)
            //if (resume_bed_temp) sprintf_P(cmd, PSTR("M190 S%i\n"), resume_bed_temp); //rock_20210901 
          #endif
          #if BOTH(HAS_HOTEND, PAUSE_HEAT)
            //if (resume_hotend_temp) sprintf_P(&cmd[strlen(cmd)], PSTR("M109 S%i\n"), resume_hotend_temp);
          #endif
          if(HMI_flag.cloud_printing_flag && !HMI_flag.filement_resume_flag)
          {
            SERIAL_ECHOLN("M79 S3");
          }
          pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
          strcat_P(cmd, M24_STR);
          queue.inject(cmd);          
          Goto_PrintProcess();
        }
        else {  //取消
          HMI_flag.select_flag = true;
          checkkey = Print_window;
          Popup_window_PauseOrStop();
        }
        break;

      case 2: // Stop
        HMI_flag.select_flag = true;
        checkkey = Print_window;
        Popup_window_PauseOrStop();       
        
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Pause and Stop window */
void HMI_PauseOrStop() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_CW)
    Draw_Select_Highlight(false);
  else if (encoder_diffState == ENCODER_DIFF_CCW)
    Draw_Select_Highlight(true);
  else if (encoder_diffState == ENCODER_DIFF_ENTER) 
  {
    if (select_print.now == 1) 
    { // pause window
      if (HMI_flag.select_flag) 
      {
        HMI_flag.pause_action = true;
        ICON_Continue();
        if(HMI_flag.cloud_printing_flag&&!HMI_flag.filement_resume_flag)
        {
           //SERIAL_ECHOLN("M79 S2");   // 3:cloud print pause
           SERIAL_ECHOLN("paused for user");
        }
        queue.inject_P(PSTR("M25"));
      }
      else  //点击过不再提示 
      {
         
      }     
      Goto_PrintProcess();
    }
    else if (select_print.now == 2) { // stop window
      if (HMI_flag.select_flag) 
      {
        if (HMI_flag.home_flag) planner.synchronize(); // Wait for planner moves to finish!
        wait_for_heatup = wait_for_user = false;       // Stop waiting for heating/user        
        HMI_flag.disallow_recovery_flag=true;  //不允许恢复数据
        //rock_20211227
        // queue.clear();
        // quickstop_stepper();
        print_job_timer.stop();
        thermalManager.disable_all_heaters();          
        print_job_timer.reset();
        thermalManager.setTargetHotend(0, 0);
        thermalManager.setTargetBed(0);
        thermalManager.zero_fan_speeds();

        recovery.info.sd_printing_flag=false;        //rock_20210820 
        //rock_20210830  下面这句绝对不能要，会进行两次会主界面操作
        //dwin_abort_flag = true;                      // Reset feedrate, return to Home  
        #ifdef ACTION_ON_CANCEL
          host_action_cancel();  //
        #endif
        // BL24CXX::EEPROM_Reset(PLR_ADDR, (uint8_t*)&recovery.info, sizeof(recovery.info));//rock_20210812  清空 EEPROM
        //checkkey = Popup_Window;
        Popup_Window_Home(true);
        
        card.abortFilePrintSoon();                     // Let the main loop handle SD abort  //rock_20211020
        checkkey = Back_Main;
        if(HMI_flag.cloud_printing_flag)
        {
          HMI_flag.cloud_printing_flag=false;
          SERIAL_ECHOLN("M79 S4");
          //gcode.process_subcommands_now_P(PSTR("M79 S4"));   // //rock_20211022 //rock_20211022  tell wif_box print stop
        }
      }
      else
        Goto_PrintProcess(); // cancel stop
    }
  }
  DWIN_UpdateLCD();
}
#if ENABLED(HAS_CHECKFILAMENT)
	/* Check filament */
void HMI_Filament() 
{
  ENCODER_DiffState encoder_diffState;
  if(READ(CHECKFILAMENT_PIN)&&HMI_flag.cloud_printing_flag)
  {
    delay(200); //防止抖动  rock_20210910
    if(READ(CHECKFILAMENT_PIN))
    {
      SERIAL_ECHOLN(STR_BUSY_PAUSED_FOR_USER); 
    }        
  }
  encoder_diffState = get_encoder_state();    
  if (encoder_diffState == ENCODER_DIFF_NO) return;
	  if(encoder_diffState == ENCODER_DIFF_CW)
	  {
      HMI_flag.select_flag = 0;
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 25, 279, 126, 318);
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 24, 278, 127, 319);
      DWIN_Draw_Rectangle(0, Select_Color, 145, 279, 246, 318);
      DWIN_Draw_Rectangle(0, Select_Color, 144, 278, 247, 319);		  
	  }
	  else if (encoder_diffState == ENCODER_DIFF_CCW)
	  {
      //SERIAL_ECHOPGM("go to ENCODER_DIFF_CCW");
      HMI_flag.select_flag = 1;
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 145, 279, 246, 318);
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 144, 278, 247, 319);
      DWIN_Draw_Rectangle(0, Select_Color, 25, 279, 126, 318);
      DWIN_Draw_Rectangle(0, Select_Color, 24, 278, 127, 319);
	  }
	  else if (encoder_diffState == ENCODER_DIFF_ENTER)
	  {
      // SERIAL_ECHOLNPAIR("HMI_flag.select_flag=: ", HMI_flag.select_flag);
      if(HMI_flag.select_flag) 
      {
        if(READ(CHECKFILAMENT_PIN) == 0)//原为if(READ(CHECKFILAMENT_PIN))------zy
        {
          delay(200); //防止抖动  rock_20210910
            if(READ(CHECKFILAMENT_PIN) == 0)//原为if(READ(CHECKFILAMENT_PIN))------zy
            {
              HMI_flag.cutting_line_flag=false;   //resume              
              temp_cutting_line_flag=false; 
              if(HMI_flag.cloud_printing_flag)
              {
                HMI_flag.filement_resume_flag=false; //解除断料状态，可以响应云指令
                // SERIAL_ECHOLN("M79 S3");
                print_job_timer.start();
                gcode.process_subcommands_now_P(PSTR("M24"));
                Goto_PrintProcess();       
                ICON_Pause();  //暂停界面      
              }
              else
              {
                if((!HMI_flag.remove_card_flag)&& (!temp_remove_card_flag)) 
                {
                  pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
                  gcode.process_subcommands_now_P(PSTR("M24"));
                  Goto_PrintProcess(); 
                }
              }
              HMI_flag.disable_queued_cmd=false;   //可以接收GCOD指令                         
            }
        }    
      }
      else //不再提示
      {   
        if (HMI_flag.home_flag) planner.synchronize(); // Wait for planner moves to finish!
        wait_for_heatup = wait_for_user = false;       // Stop waiting for heating/user        
        HMI_flag.disallow_recovery_flag=true;  //不允许恢复数据
        //rock_20211017
        queue.clear();
        quickstop_stepper();
        print_job_timer.stop();
        thermalManager.disable_all_heaters();          
        print_job_timer.reset();
        thermalManager.setTargetHotend(0, 0);
        thermalManager.setTargetBed(0);
        thermalManager.zero_fan_speeds();

        recovery.info.sd_printing_flag=false;        //rock_20210820 
        //rock_20210830  下面这句绝对不能要，会进行两次会主界面操作
        //dwin_abort_flag = true;                      // Reset feedrate, return to Home  
        #ifdef ACTION_ON_CANCEL
          host_action_cancel();  //
        #endif
        Popup_Window_Home(true);
        
        card.abortFilePrintSoon();                     // Let the main loop handle SD abort  //rock_20211020
        checkkey = Back_Main;
        if(HMI_flag.cloud_printing_flag)
        {
          HMI_flag.cloud_printing_flag=false;
          SERIAL_ECHOLN("M79 S4");
          //gcode.process_subcommands_now_P(PSTR("M79 S4"));   // //rock_20211022 //rock_20211022  tell wif_box print stop
        }
      }
	  }
	  DWIN_UpdateLCD();
}

	/* Remove_card_window */
void HMI_Remove_card() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  //ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
 
    if (encoder_diffState == ENCODER_DIFF_ENTER)
	  {      
        if(IS_SD_INSERTED()) //有卡
        {
          HMI_flag.remove_card_flag=false;
          temp_remove_card_flag=false;          
          #if ENABLED(PAUSE_HEAT)
            char cmd[20];
          #endif
          pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
          gcode.process_subcommands_now_P(PSTR("M24"));
          Goto_PrintProcess();//
          // recovery.info.sd_printing_flag=remove_card_flag;
        }
      // }
      else 
      {
        //Remove_card_window_check();
      }
	  }
	  DWIN_UpdateLCD();
}

#endif
void Draw_Move_Menu() 
{
  Clear_Main_Window();
  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 192, 1, 233, 14); // "Move"
    DWIN_Frame_AreaCopy(1, 58, 118, 106, 132, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 109, 118, 157, 132, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 160, 118, 209, 132, LBLX, MBASE(3));
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 212, 118, 253, 131, LBLX, MBASE(4));
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MOVE_AXIS));
    #else
      DWIN_Frame_TitleCopy(1, 231, 2, 265, 12);                     // "Move"
    #endif
    draw_move_en(MBASE(1)); say_x(36, MBASE(1));                    // "Move X"
    draw_move_en(MBASE(2)); say_y(36, MBASE(2));                    // "Move Y"
    draw_move_en(MBASE(3)); say_z(36, MBASE(3));                    // "Move Z"
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 123, 192, 176, 202, LBLX, MBASE(4));   // "Extruder"
    #endif
  }

  Draw_Back_First(select_axis.now == 0);
  if (select_axis.now) Draw_Menu_Cursor(select_axis.now);

  // Draw separators and icons
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MoveX + i);
}

void Draw_AdvSet_Menu() {
  Clear_Main_Window();

  #if ADVSET_CASE_TOTAL >= 6
    const int16_t scroll = MROWS - index_advset; // Scrolled-up lines
    #define ASCROL(L) (scroll + (L))
  #else
    #define ASCROL(L) (L)
  #endif

#define AVISI(L)  WITHIN(ASCROL(L), 0, MROWS)

  Draw_Title(GET_TEXT_F(MSG_ADVANCED_SETTINGS));

  if (AVISI(0)) Draw_Back_First(select_advset.now == 0);
  if (AVISI(ADVSET_CASE_HOMEOFF)) Draw_Menu_Line(ASCROL(ADVSET_CASE_HOMEOFF), ICON_HomeOff, GET_TEXT(MSG_SET_HOME_OFFSETS),true);  // Home Offset >
  #if HAS_ONESTEP_LEVELING
    if (AVISI(ADVSET_CASE_PROBEOFF)) Draw_Menu_Line(ASCROL(ADVSET_CASE_PROBEOFF), ICON_ProbeOff, GET_TEXT(MSG_ZPROBE_OFFSETS),true);  // Probe Offset >
  #endif
  if (AVISI(ADVSET_CASE_HEPID)) Draw_Menu_Line(ASCROL(ADVSET_CASE_HEPID), ICON_PIDNozzle, "Hotend PID", false);  // Nozzle PID
  if (AVISI(ADVSET_CASE_BEDPID)) Draw_Menu_Line(ASCROL(ADVSET_CASE_BEDPID), ICON_PIDbed, "Bed PID", false);  // Bed PID
  #if ENABLED(POWER_LOSS_RECOVERY)
    if (AVISI(ADVSET_CASE_PWRLOSSR)) {
      Draw_Menu_Line(ASCROL(ADVSET_CASE_PWRLOSSR), ICON_Motion, "Power-loss recovery", false);  // Power-loss recovery
      Draw_Chkb_Line(ASCROL(ADVSET_CASE_PWRLOSSR), recovery.enabled);
    }
  #endif
  if (select_advset.now) Draw_Menu_Cursor(ASCROL(select_advset.now));
}

void Draw_HomeOff_Menu() {
  Clear_Main_Window();
  Draw_Title(GET_TEXT_F(MSG_SET_HOME_OFFSETS));                 // Home Offsets
  Draw_Back_First(select_item.now == 0);
  Draw_Menu_Line(1, ICON_HomeOffX, GET_TEXT(MSG_HOME_OFFSET_X));  // Home X Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(1), HMI_ValueStruct.Home_OffX_scaled);
  Draw_Menu_Line(2, ICON_HomeOffY, GET_TEXT(MSG_HOME_OFFSET_Y));  // Home Y Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(2), HMI_ValueStruct.Home_OffY_scaled);
  Draw_Menu_Line(3, ICON_HomeOffZ, GET_TEXT(MSG_HOME_OFFSET_Z));  // Home Y Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(3), HMI_ValueStruct.Home_OffZ_scaled);
  if (select_item.now) Draw_Menu_Cursor(select_item.now);
}

#if HAS_ONESTEP_LEVELING
  void Draw_ProbeOff_Menu() {
    Clear_Main_Window();
    Draw_Title(GET_TEXT_F(MSG_ZPROBE_OFFSETS));                 // Probe Offsets
    Draw_Back_First(select_item.now == 0);
    Draw_Menu_Line(1, ICON_ProbeOffX, GET_TEXT(MSG_ZPROBE_XOFFSET));  // Probe X Offset
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(1), HMI_ValueStruct.Probe_OffX_scaled);
    Draw_Menu_Line(2, ICON_ProbeOffY, GET_TEXT(MSG_ZPROBE_YOFFSET));  // Probe Y Offset
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(2), HMI_ValueStruct.Probe_OffY_scaled);
    if (select_item.now) Draw_Menu_Cursor(select_item.now);
  }
#endif

#include "../../../libs/buzzer.h"

void HMI_AudioFeedback(const bool success=true) {
  if (success) {
    buzzer.tone(100, 659);
    buzzer.tone(10, 0);
    buzzer.tone(100, 698);
  }
  else
    buzzer.tone(40, 440);
}

/* Prepare */
void HMI_Prepare() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_prepare.inc(1 + PREPARE_CASE_TOTAL)) {
      if (select_prepare.now > MROWS && select_prepare.now > index_prepare) {
        index_prepare = select_prepare.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_Menu_Icon(MROWS, ICON_Axis + select_prepare.now - 1);

        // Draw "More" icon for sub-menus
        if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        #if HAS_HOTEND
          if (index_prepare == PREPARE_CASE_ABS) Item_Prepare_ABS(MROWS);
        #endif
        #if HAS_PREHEAT
          if (index_prepare == PREPARE_CASE_COOL) Item_Prepare_Cool(MROWS);
        #endif
        if (index_prepare == PREPARE_CASE_LANG) Item_Prepare_Lang(MROWS);
      }
      else {
        Move_Highlight(1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_prepare.dec()) {
      if (select_prepare.now < index_prepare - MROWS) {
        index_prepare--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        if (index_prepare == MROWS)
          Draw_Back_First();
        else
          Draw_Menu_Line(0, ICON_Axis + select_prepare.now - 1);

        if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

             if (index_prepare == 6) Item_Prepare_Move(0);
        else if (index_prepare == 7) Item_Prepare_Disable(0);
        else if (index_prepare == 8) Item_Prepare_Home(0);
      }
      else {
        Move_Highlight(-1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) 
  {
    
    switch (select_prepare.now) 
    {
      case 0: // Back
        select_page.set(1);
        Goto_MainMenu();
        break;
      case PREPARE_CASE_MOVE: // Axis move
        checkkey = AxisMove;
        select_axis.reset();
        Draw_Move_Menu();
        gcode.process_subcommands_now("G92 E0"); //rock_20210927 liuxu  Reset Extruder
        // current_position.e=0;  //rock_20210901 liuxu
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(1), current_position.x * MINUNITMULT);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(1), current_position.x * MINUNITMULT);
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(2), current_position.y * MINUNITMULT);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(2), current_position.y * MINUNITMULT);
        //DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(3), current_position.z * MINUNITMULT);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(3), current_position.z * MINUNITMULT);
        #if HAS_HOTEND
          HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
          DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        #endif
          if(!HMI_flag.power_back_to_zero_flag){
          // HMI_flag.power_back_to_zero_flag=false;
          //增加“回原点”操作，设备在没有进行0点定位之前，操作移动轴，有撞击极限位值的风险。rock_20211025 在没有
             HMI_flag.power_back_to_zero_flag=true; //rock_21211025
            checkkey = Last_Prepare;
            index_prepare = MROWS;
            queue.inject_P(G28_STR); // G28 will set home_flag
             Popup_Window_Home();   //rock_20211025
            //Electricity_back_to_zero(0);
          }
        break;
      case PREPARE_CASE_DISA: // Disable steppers
        queue.inject_P(PSTR("M84"));
        gcode.process_subcommands_now("G92.9Z0"); //rock_20211224 解决人为下降Z轴，导致撞平台的问题。
        break;
      case PREPARE_CASE_HOME: // Homing
        HMI_flag.power_back_to_zero_flag=true; //rock_21211025
        checkkey = Last_Prepare;
        index_prepare = MROWS;
        queue.inject_P(G28_STR); // G28 will set home_flag
        Popup_Window_Home();
        break;
      #if HAS_ZOFFSET_ITEM
        case PREPARE_CASE_ZOFF: // Z-offset
          #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
            checkkey = Homeoffset;
            HMI_ValueStruct.show_mode = -4;
            HMI_ValueStruct.offset_value = BABY_Z_VAR * 100;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, 202, MBASE(PREPARE_CASE_ZOFF + MROWS - index_prepare), HMI_ValueStruct.offset_value);
            EncoderRate.enabled = true;
          #else
            // Apply workspace offset, making the current position 0,0,0
            queue.inject_P(PSTR("G92 X0 Y0 Z0"));
            HMI_AudioFeedback();
          #endif
          break;
      #endif
      #if HAS_PREHEAT
        case PREPARE_CASE_PLA: // PLA preheat
          TERN_(HAS_HOTEND, thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0));
          TERN_(HAS_HEATED_BED, thermalManager.setTargetBed(ui.material_preset[0].bed_temp));
          TERN_(HAS_FAN, thermalManager.set_fan_speed(0, ui.material_preset[0].fan_speed));
          break;
        case PREPARE_CASE_ABS: // ABS preheat
          TERN_(HAS_HOTEND, thermalManager.setTargetHotend(ui.material_preset[1].hotend_temp, 0));
          TERN_(HAS_HEATED_BED, thermalManager.setTargetBed(ui.material_preset[1].bed_temp));
          TERN_(HAS_FAN, thermalManager.set_fan_speed(0, ui.material_preset[1].fan_speed));
          break;
        case PREPARE_CASE_COOL: // Cool
          TERN_(HAS_FAN, thermalManager.zero_fan_speeds());
          #if HAS_HOTEND || HAS_HEATED_BED
            thermalManager.disable_all_heaters();
          #endif
          break;
      #endif
      case PREPARE_CASE_LANG: // Toggle Language
        HMI_ToggleLanguage();
        Draw_Prepare_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

void Draw_Temperature_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 236, 2, 263, 13); // "Temperature"
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 1, 134, 56, 146, LBLX, MBASE(TEMP_CASE_TEMP));
    #endif
    #if HAS_HEATED_BED
      DWIN_Frame_AreaCopy(1, 58, 134, 113, 146, LBLX, MBASE(TEMP_CASE_BED));
    #endif
    #if HAS_FAN
      DWIN_Frame_AreaCopy(1, 115, 134, 172, 146, LBLX, MBASE(TEMP_CASE_FAN));
    #endif
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 100, 87, 178, 101, LBLX, MBASE(TEMP_CASE_PLA));  //rock_20210727
      DWIN_Frame_AreaCopy(1, 180, 87, 260, 100, LBLX, MBASE(TEMP_CASE_ABS));
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_TEMPERATURE));
    #else
      DWIN_Frame_TitleCopy(1, 56, 16, 141, 28);                                       // "Temperature"
    #endif
    #ifdef USE_STRING_TITLES
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(TEMP_CASE_TEMP), GET_TEXT_F(MSG_UBL_SET_TEMP_HOTEND));
      #endif
      #if HAS_HEATED_BED
        DWIN_Draw_Label(MBASE(TEMP_CASE_BED), GET_TEXT_F(MSG_UBL_SET_TEMP_BED));
      #endif
      #if HAS_FAN
        DWIN_Draw_Label(MBASE(TEMP_CASE_FAN), GET_TEXT_F(MSG_FAN_SPEED));
      #endif
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(TEMP_CASE_PLA), F("PLA Preheat Settings"));
        DWIN_Draw_Label(MBASE(TEMP_CASE_ABS), F("ABS Preheat Settings"));
      #endif
    #else
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 197, 104, 240, 116, LBLX, MBASE(TEMP_CASE_TEMP));      // Nozzle...
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 46, MBASE(TEMP_CASE_TEMP));     // ...Temperature
      #endif
      #if HAS_HEATED_BED
        DWIN_Frame_AreaCopy(1, 240, 104, 264, 114, LBLX, MBASE(TEMP_CASE_BED));       // Bed...
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 27, MBASE(TEMP_CASE_BED));      // ...Temperature
      #endif
      #if HAS_FAN
        DWIN_Frame_AreaCopy(1, 0, 119, 64, 132, LBLX, MBASE(TEMP_CASE_FAN));          // Fan speed
      #endif
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(TEMP_CASE_PLA));         // Preheat...
        DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX + 52, MBASE(TEMP_CASE_PLA));    // ...PLA
        DWIN_Frame_AreaCopy(1, 131, 119, 182, 132, LBLX + 79, MBASE(TEMP_CASE_PLA));  // PLA setting
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(TEMP_CASE_ABS));         // Preheat...
        DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX + 52, MBASE(TEMP_CASE_ABS));    // ...ABS
        DWIN_Frame_AreaCopy(1, 131, 119, 182, 132, LBLX + 81, MBASE(TEMP_CASE_ABS));  // ABS setting
      #endif
    #endif
  }

  Draw_Back_First(select_temp.now == 0);
  if (select_temp.now) Draw_Menu_Cursor(select_temp.now);

  // Draw icons and lines
  uint8_t i = 0;
  #define _TMENU_ICON(N) Draw_Menu_Line(++i, ICON_SetEndTemp + (N) - 1)
  #if HAS_HOTEND
    _TMENU_ICON(TEMP_CASE_TEMP);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), thermalManager.degTargetHotend(0));
  #endif
  #if HAS_HEATED_BED
    _TMENU_ICON(TEMP_CASE_BED);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), thermalManager.degTargetBed());
  #endif
  #if HAS_FAN
    _TMENU_ICON(TEMP_CASE_FAN);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), thermalManager.fan_speed[0]);
  #endif
  #if HAS_HOTEND
    // PLA/ABS items have submenus
    _TMENU_ICON(TEMP_CASE_PLA);
    Draw_More_Icon(i);
    _TMENU_ICON(TEMP_CASE_ABS);
    Draw_More_Icon(i);
  #endif
}

/* Control */
void HMI_Control() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_control.inc(1 + CONTROL_CASE_TOTAL)) {
      if (select_control.now > MROWS && select_control.now > index_control) 
      {
        index_control = select_control.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);

        switch (index_control) {  // Last menu items
        /*
          case CONTROL_CASE_ADVSET:  // Advanced Settings >
            Draw_Menu_Item(MROWS, ICON_AdvSet, GET_TEXT(MSG_ADVANCED_SETTINGS), true);
            break;
        */
          case CONTROL_CASE_INFO:    // Info >
            Item_Control_Info(MBASE(MROWS));
            Draw_Menu_Icon(MROWS, ICON_Info);
            break;
          default: break;
        }

      }
      else {
        Move_Highlight(1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_control.dec()) {
      if (select_control.now < index_control - MROWS) {
        index_control--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        switch (index_control) {  // First menu items
          case MROWS :
          Draw_Back_First();
            break;
          case MROWS + 1: // Temperature >
            // Draw_Menu_Line(0, ICON_Temperature, GET_TEXT(MSG_TEMPERATURE), true);
            // DWIN_Frame_AreaCopy(1,  57, 104,  84, 116, LBLX, CLINE(CONTROL_CASE_TEMP));   // Temperature >
            if (HMI_IsChinese())
            {
              Draw_Menu_Icon(0, ICON_Temperature);
              Draw_More_Icon(0);
              DWIN_Frame_AreaCopy(1, 57, 104,  84, 116, 60, 53);
              DWIN_Draw_Line(Line_Color, 16, 49 + 33, 256, 49 + 34);
            }
            else
            {
              Draw_Menu_Icon(0, ICON_Temperature);
              Draw_More_Icon(0);
              DWIN_Frame_AreaCopy(1,  1, 89,  83, 101, 60, 53);   //DWIN_Frame_AreaCopy(1,  1, 89,  83, 101, LBLX, CLINE(CONTROL_CASE_TEMP)); 
              DWIN_Draw_Line(Line_Color, 16, 49 + 33, 256, 49 + 34);
            }

            break;
          case MROWS + 2: // Move >
            Draw_Menu_Line(0, ICON_Motion, GET_TEXT(MSG_MOTION), true);
            // DWIN_Frame_AreaCopy(1,  87, 104, 114, 116, 60, 155);   // Motion >
          default: break;
        }
      }
      else {
        Move_Highlight(-1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_control.now) {
      case 0: // Back
        select_page.set(2);
        Goto_MainMenu();
        break;
      case CONTROL_CASE_TEMP:  // Temperature
        checkkey = TemperatureID;
        HMI_ValueStruct.show_mode = -1;
        select_temp.reset();
        Draw_Temperature_Menu();
        break;
      case CONTROL_CASE_MOVE: // Motion
        checkkey = Motion;
        select_motion.reset();
        Draw_Motion_Menu();
        break;
      #if ENABLED(EEPROM_SETTINGS)
        case CONTROL_CASE_SAVE: { // Write EEPROM
          const bool success = settings.save();
          HMI_AudioFeedback(success);
        } break;
        case CONTROL_CASE_LOAD: { // Read EEPROM
          const bool success = settings.load();
          HMI_AudioFeedback(success);
        } break;
        case CONTROL_CASE_RESET: // Reset EEPROM
          HMI_ResetLanguage();
          settings.reset();
          HMI_AudioFeedback();
          settings.save();
          delay(300);
          break;
      #endif
      /* //rock_20210727
      case CONTROL_CASE_ADVSET: // Advanced Settings
        checkkey = AdvSet;
        select_advset.reset();
        Draw_AdvSet_Menu();
        break;
      */
      case CONTROL_CASE_INFO: // Info
        checkkey = Info;
        Draw_Info_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}


#if HAS_ONESTEP_LEVELING

  /* Leveling */
  void HMI_Leveling() {
    Popup_Window_Leveling();
    DWIN_UpdateLCD();
    // thermalManager.disable_all_heaters();  //rock_20211223
    queue.inject_P(PSTR("G28""\n""G29"));
  }

#endif

/* Axis Move */
void HMI_AxisMove() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  #if ENABLED(PREVENT_COLD_EXTRUSION)
    // popup window resume
    if (HMI_flag.ETempTooLow_flag) 
    {
      if (encoder_diffState == ENCODER_DIFF_ENTER) 
      {
        HMI_flag.ETempTooLow_flag = false;
        HMI_ValueStruct.Move_X_scaled = current_position.x * MINUNITMULT;  //rock_20210827
        HMI_ValueStruct.Move_Y_scaled = current_position.y * MINUNITMULT;  //rock_20210827
        HMI_ValueStruct.Move_Z_scaled = current_position.z * MINUNITMULT;
        HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
        Draw_Move_Menu();
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        DWIN_UpdateLCD();
        return;
      }      
    }
  #endif

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_axis.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_axis.dec()) Move_Highlight(-1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_axis.now) {
      case 0: // Back
        checkkey = Prepare;
        select_prepare.set(1);
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;
      case 1: // X axis move
        checkkey = Move_X;
        HMI_ValueStruct.Move_X_scaled = current_position.x * MINUNITMULT;
       // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, 216,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
        EncoderRate.enabled = true;
        break;
      case 2: // Y axis move
        checkkey = Move_Y;
        HMI_ValueStruct.Move_Y_scaled = current_position.y * MINUNITMULT;
        //DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, 216, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        EncoderRate.enabled = true;
        break;
      case 3: // Z axis move
        checkkey = Move_Z;
        HMI_ValueStruct.Move_Z_scaled = current_position.z * MINUNITMULT;
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        EncoderRate.enabled = true;
        break;
        #if HAS_HOTEND
          case 4: // Extruder
            // window tips
           
            #ifdef PREVENT_COLD_EXTRUSION
              if (thermalManager.tooColdToExtrude(0)) {
                HMI_flag.ETempTooLow_flag = true;
                Popup_Window_ETempTooLow();
                DWIN_UpdateLCD();
                return;
              }
            #endif
             checkkey = Extruder;
            HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scaled);
            EncoderRate.enabled = true;
            break;
        #endif
    }
  }
  DWIN_UpdateLCD();
}

/* TemperatureID */
void HMI_Temperature() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_temp.inc(1 + TEMP_CASE_TOTAL)) Move_Highlight(1, select_temp.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_temp.dec()) Move_Highlight(-1, select_temp.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_temp.now) {
      case 0: // Back
        checkkey = Control;
        select_control.set(1);
        index_control = MROWS;
        Draw_Control_Menu();
        break;
      #if HAS_HOTEND
        case TEMP_CASE_TEMP: // Nozzle temperature
          checkkey = ETemp;
          HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(1), HMI_ValueStruct.E_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_HEATED_BED
        case TEMP_CASE_BED: // Bed temperature
          checkkey = BedTemp;
          HMI_ValueStruct.Bed_Temp = thermalManager.degTargetBed();
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(2), HMI_ValueStruct.Bed_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_FAN
        case TEMP_CASE_FAN: // Fan speed
          checkkey = FanSpeed;
          HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(3), HMI_ValueStruct.Fan_speed);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_HOTEND
        case TEMP_CASE_PLA: { // PLA preheat setting
          checkkey = PLAPreheat;
          select_PLA.reset();
          HMI_ValueStruct.show_mode = -2;

          Clear_Main_Window();

          if (HMI_IsChinese()) {
            DWIN_Frame_TitleCopy(1, 59, 16, 139, 29);                                         // "PLA Settings"
            DWIN_Frame_AreaCopy(1, 100, 89, 124, 101, LBLX, MBASE(PREHEAT_CASE_TEMP));
            DWIN_Frame_AreaCopy(1, 1, 134, 56, 146, LBLX + 24, MBASE(PREHEAT_CASE_TEMP));     // PLA nozzle temp
            #if HAS_HEATED_BED
              DWIN_Frame_AreaCopy(1, 100, 89, 124, 101, LBLX, MBASE(PREHEAT_CASE_BED));
              DWIN_Frame_AreaCopy(1, 58, 134, 113, 146, LBLX + 24, MBASE(PREHEAT_CASE_BED));  // PLA bed temp
            #endif
            #if HAS_FAN
              DWIN_Frame_AreaCopy(1, 100, 89, 124, 101, LBLX, MBASE(PREHEAT_CASE_FAN));
              DWIN_Frame_AreaCopy(1, 115, 134, 172, 146, LBLX + 24, MBASE(PREHEAT_CASE_FAN)); // PLA fan speed
            #endif
            #if ENABLED(EEPROM_SETTINGS)
              DWIN_Frame_AreaCopy(1, 72, 148, 151, 162, LBLX, MBASE(PREHEAT_CASE_SAVE));      // Save PLA configuration
            #endif
          }
          else {
            #ifdef USE_STRING_HEADINGS
              Draw_Title("PLA Settings"); // TODO: GET_TEXT_F
            #else
              DWIN_Frame_TitleCopy(1, 56, 16, 141, 28);                                       // "PLA Settings"
            #endif
            #ifdef USE_STRING_TITLES
              DWIN_Draw_Label(MBASE(PREHEAT_CASE_TEMP), F("Nozzle Temp"));
              #if HAS_HEATED_BED
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_BED), F("Bed Temp"));
              #endif
              #if HAS_FAN
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_FAN), GET_TEXT_F(MSG_FAN_SPEED));
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_SAVE), GET_TEXT_F(MSG_STORE_EEPROM));
              #endif
            #else
              DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX, MBASE(PREHEAT_CASE_TEMP)+3);
              DWIN_Frame_AreaCopy(1, 197, 104, 240, 116, LBLX + 27, MBASE(PREHEAT_CASE_TEMP)+3);
              DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 71, MBASE(PREHEAT_CASE_TEMP)+3);      // PLA nozzle temp
              #if HAS_HEATED_BED
                DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX, MBASE(PREHEAT_CASE_BED) +3);
                DWIN_Frame_AreaCopy(1, 240, 104, 264, 114, LBLX + 27, MBASE(PREHEAT_CASE_BED)+3);
                DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 54, MBASE(PREHEAT_CASE_BED)+3); // PLA bed temp
              #endif
              #if HAS_FAN
                DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX, MBASE(PREHEAT_CASE_FAN));
                DWIN_Frame_AreaCopy(1, 0, 119, 64, 132, LBLX + 27, MBASE(PREHEAT_CASE_FAN));    // PLA fan speed
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_Frame_AreaCopy(1, 97, 165, 229, 177, LBLX, MBASE(PREHEAT_CASE_SAVE));      // Save PLA configuration
              #endif
            #endif
          }

          Draw_Back_First();

          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif
        } break;

        case TEMP_CASE_ABS: { // ABS preheat setting
          checkkey = ABSPreheat;
          select_ABS.reset();
          HMI_ValueStruct.show_mode = -3;

          Clear_Main_Window();

          if (HMI_IsChinese()) {
            DWIN_Frame_TitleCopy(1, 142, 16, 223, 29);                                        // "ABS Settings"

            DWIN_Frame_AreaCopy(1, 180, 89, 204, 100, LBLX, MBASE(PREHEAT_CASE_TEMP));
            DWIN_Frame_AreaCopy(1, 1, 134, 56, 146, LBLX + 24, MBASE(PREHEAT_CASE_TEMP));    // ABS nozzle temp
            #if HAS_HEATED_BED
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 100, LBLX, MBASE(PREHEAT_CASE_BED));
              DWIN_Frame_AreaCopy(1, 58, 134, 113, 146, LBLX + 24, MBASE(PREHEAT_CASE_BED));  // ABS bed temp
            #endif
            #if HAS_FAN
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 100, LBLX, MBASE(PREHEAT_CASE_FAN));
              DWIN_Frame_AreaCopy(1, 115, 134, 172, 146, LBLX + 24, MBASE(PREHEAT_CASE_FAN)); // ABS fan speed
            #endif
            #if ENABLED(EEPROM_SETTINGS)
              DWIN_Frame_AreaCopy(1, 72, 148, 151, 162, LBLX, MBASE(PREHEAT_CASE_SAVE));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 100, LBLX + 28, MBASE(PREHEAT_CASE_SAVE) + 2);   // Save ABS configuration
            #endif
          }
          else {
            #ifdef USE_STRING_HEADINGS
              Draw_Title("ABS Settings"); // TODO: GET_TEXT_F
            #else
              DWIN_Frame_TitleCopy(1, 56, 16, 141, 28);                                                  // "ABS Settings"
            #endif
            #ifdef USE_STRING_TITLES
              DWIN_Draw_Label(MBASE(PREHEAT_CASE_TEMP), F("Nozzle Temp"));
              #if HAS_HEATED_BED
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_BED), F("Bed Temp"));
              #endif
              #if HAS_FAN
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_FAN), GET_TEXT_F(MSG_FAN_SPEED));
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_Draw_Label(MBASE(PREHEAT_CASE_SAVE), GET_TEXT_F(MSG_STORE_EEPROM));
              #endif
            #else
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX, MBASE(PREHEAT_CASE_TEMP)+3);
              DWIN_Frame_AreaCopy(1, 197, 104, 240, 116, LBLX + 27, MBASE(PREHEAT_CASE_TEMP)+3);
              DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 71, MBASE(PREHEAT_CASE_TEMP)+3);      // ABS nozzle temp
              #if HAS_HEATED_BED
                DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX, MBASE(PREHEAT_CASE_BED)+3 );
                DWIN_Frame_AreaCopy(1, 240, 104, 264, 114, LBLX + 27, MBASE(PREHEAT_CASE_BED) +3);
                DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 54, MBASE(PREHEAT_CASE_BED)+3 ); // ABS bed temp
              #endif
              #if HAS_FAN
                DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX, MBASE(PREHEAT_CASE_FAN));
                DWIN_Frame_AreaCopy(1, 0, 119, 64, 132, LBLX + 27, MBASE(PREHEAT_CASE_FAN));             // ABS fan speed
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_Frame_AreaCopy(1, 97, 165, 229, 177, LBLX, MBASE(PREHEAT_CASE_SAVE));
                DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX + 33, MBASE(PREHEAT_CASE_SAVE));                     // Save ABS configuration
              #endif
            #endif
          }

          Draw_Back_First();

          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif

        } break;

      #endif // HAS_HOTEND
    }
  }
  DWIN_UpdateLCD();
}

void Draw_Max_Speed_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Max Speed (mm/s)"

    auto say_max_speed = [](const uint16_t row) {
      DWIN_Frame_AreaCopy(1, 173, 133, 228, 147, LBLX, row);              // "Max speed"
    };

    say_max_speed(MBASE(1));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 58, 120, 65, 131, LBLX + 58, MBASE(1)+3);      // X  rock_20210919
    say_max_speed(MBASE(2));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 58, MBASE(2) + 3);      // Y
    say_max_speed(MBASE(3));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 58, MBASE(3) + 3);     // Z
    #if HAS_HOTEND
      say_max_speed(MBASE(4));                                            // "Max speed"
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 58, MBASE(4) + 3);  // E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Max Speed (mm/s)"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Max Speed (mm/s)"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(1), F("Max Feedrate X"));
      DWIN_Draw_Label(MBASE(2), F("Max Feedrate Y"));
      DWIN_Draw_Label(MBASE(3), F("Max Feedrate Z"));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(4), F("Max Feedrate E"));
      #endif
    #else
      draw_max_en(MBASE(1));          // "Max"
      DWIN_Frame_AreaCopy(1, 184, 119, 234, 132, LBLX + 30, MBASE(1)); // "Speed X"

      draw_max_en(MBASE(2));          // "Max"
      draw_speed_en(27, MBASE(2));    // "Speed" rock_20210919
      say_y(78, MBASE(2));            // "Y"

      draw_max_en(MBASE(3));          // "Max"
      draw_speed_en(27, MBASE(3));    // "Speed"
      say_z(78, MBASE(3));            // "Z"

      #if HAS_HOTEND
        draw_max_en(MBASE(4));        // "Max"
        draw_speed_en(27, MBASE(4));  // "Speed"
        say_e(78, MBASE(4));          // "E"
      #endif
    #endif
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedX + i);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1), planner.settings.max_feedrate_mm_s[X_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2), planner.settings.max_feedrate_mm_s[Y_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3), planner.settings.max_feedrate_mm_s[Z_AXIS]);
  #if HAS_HOTEND
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4), planner.settings.max_feedrate_mm_s[E_AXIS]);
  #endif
}

void Draw_Max_Accel_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Acceleration"

    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(1) + 1);
    DWIN_Frame_AreaCopy(1, 58, 120, 65, 131, LBLX +  + 71, MBASE(1)+3);   // Max acceleration X
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(2) + 1);
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 71, MBASE(2) + 2);   // Max acceleration Y
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(3));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(3) + 1);
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 71, MBASE(3) + 2);  // Max acceleration Z
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(4));
      DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(4) + 1);
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 71, MBASE(4) + 2); // Max acceleration E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_ACCELERATION));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);          // "Acceleration"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(1), F("Max Accel X"));
      DWIN_Draw_Label(MBASE(2), F("Max Accel Y"));
      DWIN_Draw_Label(MBASE(3), F("Max Accel Z"));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(4), F("Max Accel E"));
      #endif
    #else
      draw_max_accel_en(MBASE(1)); say_x(110, MBASE(1));  // "Max Acceleration X"
      draw_max_accel_en(MBASE(2)); say_y(110, MBASE(2));  // "Max Acceleration Y"
      draw_max_accel_en(MBASE(3)); say_z(110, MBASE(3));  // "Max Acceleration Z"
      #if HAS_HOTEND
        draw_max_accel_en(MBASE(4)); say_e(110, MBASE(4)); // "Max Acceleration E"
      #endif
    #endif
  }
  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxAccX + i);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1), planner.settings.max_acceleration_mm_per_s2[X_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2), planner.settings.max_acceleration_mm_per_s2[Y_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3), planner.settings.max_acceleration_mm_per_s2[Z_AXIS]);
  #if HAS_HOTEND
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4), planner.settings.max_acceleration_mm_per_s2[E_AXIS]); //rock_20211009
  #endif
}

#if HAS_CLASSIC_JERK
  void Draw_Max_Jerk_Menu() 
  {
    Clear_Main_Window();
    if (HMI_IsChinese()) {
      DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Jerk"

      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(1));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(1) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(1));
      DWIN_Frame_AreaCopy(1, 58, 120, 65, 131, LBLX +  + 83, MBASE(1)+3);        // Max Jerk speed X
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(2));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(2) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(2));
      DWIN_Frame_AreaCopy(1,   1, 150,   7, 160, LBLX + 83, MBASE(2) + 3);    // Max Jerk speed Y
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(3));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(3) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(3));
      DWIN_Frame_AreaCopy(1,   9, 150,  16, 160, LBLX + 83, MBASE(3) + 3);    // Max Jerk speed Z
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(4));
        DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(4) + 1);
        DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(4));
        DWIN_Frame_AreaCopy(1,  18, 150,  25, 160, LBLX + 83, MBASE(4) + 3);  // Max Jerk speed E
      #endif
    }
    else {
      #ifdef USE_STRING_HEADINGS
        Draw_Title(GET_TEXT_F(MSG_JERK));
      #else
        DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Jerk"
      #endif
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(1), F("Max Jerk X"));
        DWIN_Draw_Label(MBASE(2), F("Max Jerk Y"));
        DWIN_Draw_Label(MBASE(3), F("Max Jerk Z"));
        #if HAS_HOTEND
          DWIN_Draw_Label(MBASE(4), F("Max Jerk E"));
        #endif
      #else
        draw_max_en(MBASE(1));          // "Max"
        draw_jerk_en(MBASE(1));         // "Jerk"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77,MBASE(1)); // "Speed"
        //draw_speed_en(80, MBASE(1));    // "Speed"
        say_x(115+6, MBASE(1));           // "X"

        draw_max_en(MBASE(2));          // "Max"
        draw_jerk_en(MBASE(2));         // "Jerk"
        //draw_speed_en(80, MBASE(2));    // "Speed"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(2)); // "Speed"
        say_y(115+6, MBASE(2));           // "Y"

        draw_max_en(MBASE(3));          // "Max"
        draw_jerk_en(MBASE(3));         // "Jerk"
        //draw_speed_en(80, MBASE(3));    // "Speed"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(3)); // "Speed"
        say_z(115+6, MBASE(3));           // "Z"

        #if HAS_HOTEND
          draw_max_en(MBASE(4));        // "Max"
          draw_jerk_en(MBASE(4));       // "Jerk"
          //draw_speed_en(72, MBASE(4));  // "Speed"
          DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(4)); // "Speed"
          say_e(115+6, MBASE(4));         // "E"
        #endif
      #endif
    }

    Draw_Back_First();
    LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedJerkX + i);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(1), planner.max_jerk[X_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(2), planner.max_jerk[Y_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(3), planner.max_jerk[Z_AXIS] * MINUNITMULT+0.0002);
    #if HAS_HOTEND
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(4), planner.max_jerk[E_AXIS] * MINUNITMULT+0.0002);
    #endif
  }
#endif

void Draw_Steps_Menu() 
{
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Steps per mm"

    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 58, 120, 65, 131, LBLX  + 44, MBASE(1)+3); // Transmission Ratio X  rock_20210907
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 44, MBASE(2) + 3); // Transmission Ratio Y
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(3));
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 44, MBASE(3) + 3); // Transmission Ratio Z
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(4));
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 44, MBASE(4) + 3); // Transmission Ratio E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_STEPS_PER_MM));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Steps per mm"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(1), F("Steps/mm X"));
      DWIN_Draw_Label(MBASE(2), F("Steps/mm Y"));
      DWIN_Draw_Label(MBASE(3), F("Steps/mm Z"));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(4), F("Steps/mm E"));
      #endif
    #else
      draw_steps_per_mm(MBASE(1)); say_x(103+16, MBASE(1)); // "Steps-per-mm X"  rock_20210907
      draw_steps_per_mm(MBASE(2)); say_y(103+16, MBASE(2)); // "Y"
      draw_steps_per_mm(MBASE(3)); say_z(103+16, MBASE(3)); // "Z"
      #if HAS_HOTEND
        draw_steps_per_mm(MBASE(4)); say_e(103+16, MBASE(4)); // "E"
      #endif
    #endif
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_StepX + i);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(1), planner.settings.axis_steps_per_mm[X_AXIS] * MINUNITMULT+0.0002);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(2), planner.settings.axis_steps_per_mm[Y_AXIS] * MINUNITMULT+0.0002);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(3), planner.settings.axis_steps_per_mm[Z_AXIS] * MINUNITMULT+0.0002);
  #if HAS_HOTEND
    //rock_20210907  迪文屏显示浮点数424.9有异常所以+0.0002
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3,UNITFDIGITS, 210, MBASE(4), (planner.settings.axis_steps_per_mm[E_AXIS] * MINUNITMULT+0.0002));
  #endif
}

/* Motion */
void HMI_Motion() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_motion.inc(1 + MOTION_CASE_TOTAL)) Move_Highlight(1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_motion.dec()) Move_Highlight(-1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_motion.now) {
      case 0: // Back
        checkkey = Control;
        select_control.set(CONTROL_CASE_MOVE);
        index_control = MROWS;
        Draw_Control_Menu();
        break;
      case MOTION_CASE_RATE:   // Max speed
        checkkey = MaxSpeed;
        select_speed.reset();
        Draw_Max_Speed_Menu();
        break;
      case MOTION_CASE_ACCEL:  // Max acceleration
        checkkey = MaxAcceleration;
        select_acc.reset();
        Draw_Max_Accel_Menu();
        break;
      #if HAS_CLASSIC_JERK
        case MOTION_CASE_JERK: // Max jerk
          checkkey = MaxJerk;
          select_jerk.reset();
          Draw_Max_Jerk_Menu();
         break;
      #endif
      case MOTION_CASE_STEPS:  // Steps per mm
        checkkey = Step;
        select_step.reset();
        Draw_Steps_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Advanced Settings */
void HMI_AdvSet() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_advset.inc(1 + ADVSET_CASE_TOTAL)) {
      if (select_advset.now > MROWS && select_advset.now > index_advset) {
        index_advset = select_advset.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);

        //switch (index_advset) {  // Redraw last menu items
        //  default: break;
        //}

      }
      else {
        Move_Highlight(1, select_advset.now + MROWS - index_advset);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_advset.dec()) {
      if (select_advset.now < index_advset - MROWS) {
        index_advset--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        //switch (index_advset) {  // Redraw first menu items
        //  default: break;
        //}
      }
      else {
        Move_Highlight(-1, select_advset.now + MROWS - index_advset);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_advset.now) {
      case 0: // Back
      /*  //rock_20210727
        checkkey = Control;
        select_control.set(CONTROL_CASE_ADVSET);
        index_control = CONTROL_CASE_ADVSET;
        Draw_Control_Menu();
        */
        break;

      #if HAS_HOME_OFFSET
        case ADVSET_CASE_HOMEOFF:   // Home Offsets
          checkkey = HomeOff;
          select_item.reset();
          HMI_ValueStruct.Home_OffX_scaled = home_offset[X_AXIS] * 10;
          HMI_ValueStruct.Home_OffY_scaled = home_offset[Y_AXIS] * 10;
          HMI_ValueStruct.Home_OffZ_scaled = home_offset[Z_AXIS] * 10;
          Draw_HomeOff_Menu();
          break;
      #endif

      #if HAS_ONESTEP_LEVELING
        case ADVSET_CASE_PROBEOFF:   // Probe Offsets
          checkkey = ProbeOff;
          select_item.reset();
          HMI_ValueStruct.Probe_OffX_scaled = probe.offset.x * 10;
          HMI_ValueStruct.Probe_OffY_scaled = probe.offset.y * 10;
          Draw_ProbeOff_Menu();
          break;
      #endif

      #if HAS_HOTEND
        case ADVSET_CASE_HEPID:   // Nozzle PID Autotune
          thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0);
          thermalManager.PID_autotune(ui.material_preset[0].hotend_temp, H_E0, 10, true);
          break;
      #endif

      #if HAS_HEATED_BED
        case ADVSET_CASE_BEDPID:  // Bed PID Autotune
          thermalManager.setTargetBed(ui.material_preset[0].bed_temp);
          thermalManager.PID_autotune(ui.material_preset[0].bed_temp, H_BED, 10, true);
          break;
      #endif

      #if ENABLED(POWER_LOSS_RECOVERY)
        case ADVSET_CASE_PWRLOSSR:  // Power-loss recovery
          recovery.enable(!recovery.enabled);
          Draw_Chkb_Line(ADVSET_CASE_PWRLOSSR + MROWS - index_advset, recovery.enabled);
          break;
      #endif
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_HOME_OFFSET

  /* Home Offset */
  void HMI_HomeOff() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_item.inc(1 + 3)) Move_Highlight(1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_item.dec()) Move_Highlight(-1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_item.now) {
        case 0: // Back
          checkkey = AdvSet;
          select_advset.set(ADVSET_CASE_HOMEOFF);
          //Draw_AdvSet_Menu(); //rock_20210724
          break;
        case 1: // Home Offset X
          checkkey = HomeOffX;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(1), HMI_ValueStruct.Home_OffX_scaled);
          EncoderRate.enabled = true;
          break;
        case 2: // Home Offset Y
          checkkey = HomeOffY;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(2), HMI_ValueStruct.Home_OffY_scaled);
          EncoderRate.enabled = true;
          break;
        case 3: // Home Offset Z
          checkkey = HomeOffZ;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(3), HMI_ValueStruct.Home_OffZ_scaled);
          EncoderRate.enabled = true;
          break;
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

  void HMI_HomeOffN(const AxisEnum axis, float &posScaled, const_float_t lo, const_float_t hi) {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      if (Apply_Encoder(encoder_diffState, posScaled)) {
        checkkey = HomeOff;
        EncoderRate.enabled = false;
        set_home_offset(axis, posScaled / 10);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(select_item.now), posScaled);
        return;
      }
      LIMIT(posScaled, lo, hi);
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(select_item.now), posScaled);
    }
  }

  void HMI_HomeOffX() { HMI_HomeOffN(X_AXIS, HMI_ValueStruct.Home_OffX_scaled, -500, 500); }
  void HMI_HomeOffY() { HMI_HomeOffN(Y_AXIS, HMI_ValueStruct.Home_OffY_scaled, -500, 500); }
  void HMI_HomeOffZ() { HMI_HomeOffN(Z_AXIS, HMI_ValueStruct.Home_OffZ_scaled,  -20,  20); }

#endif // HAS_HOME_OFFSET

#if HAS_ONESTEP_LEVELING
  /*Probe Offset */
  void HMI_ProbeOff() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_item.inc(1 + 2)) Move_Highlight(1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_item.dec()) Move_Highlight(-1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_item.now) {
        case 0: // Back
          checkkey = AdvSet;
          select_advset.set(ADVSET_CASE_PROBEOFF);
          //Draw_AdvSet_Menu();
          break;
        case 1: // Probe Offset X
          checkkey = ProbeOffX;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(1), HMI_ValueStruct.Probe_OffX_scaled);
          EncoderRate.enabled = true;
          break;
        case 2: // Probe Offset X
          checkkey = ProbeOffY;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(2), HMI_ValueStruct.Probe_OffY_scaled);
          EncoderRate.enabled = true;
          break;
      }
    }
    DWIN_UpdateLCD();
  }

  void HMI_ProbeOffN(float &posScaled, float &offset_ref) {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      if (Apply_Encoder(encoder_diffState, posScaled)) {
        checkkey = ProbeOff;
        EncoderRate.enabled = false;
        offset_ref = posScaled / 10;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(select_item.now), posScaled);
        return;
      }
      LIMIT(posScaled, -500, 500);
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, 216, MBASE(select_item.now), posScaled);
    }
  }

  void HMI_ProbeOffX() { HMI_ProbeOffN(HMI_ValueStruct.Probe_OffX_scaled, probe.offset.x); }
  void HMI_ProbeOffY() { HMI_ProbeOffN(HMI_ValueStruct.Probe_OffY_scaled, probe.offset.y); }

#endif // HAS_ONESTEP_LEVELING

/* Info */
void HMI_Info() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) {
    #if HAS_ONESTEP_LEVELING
      checkkey = Control;
      select_control.set(CONTROL_CASE_INFO);
      Draw_Control_Menu();
    #else
      select_page.set(3);
      Goto_MainMenu();
    #endif
  }
  DWIN_UpdateLCD();
}

/* Tune */
void HMI_Tune() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_tune.inc(1 + TUNE_CASE_TOTAL)) {
      if (select_tune.now > MROWS && select_tune.now > index_tune) {
        index_tune = select_tune.now;
        Scroll_Menu(DWIN_SCROLL_UP);
      }
      else {
        Move_Highlight(1, select_tune.now + MROWS - index_tune);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_tune.dec()) {
      if (select_tune.now < index_tune - MROWS) {
        index_tune--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        if (index_tune == MROWS) Draw_Back_First();
      }
      else {
        Move_Highlight(-1, select_tune.now + MROWS - index_tune);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_tune.now) {
      case 0: 
      { // Back
        select_print.set(0);
        Goto_PrintProcess();
      }
      break;
      case TUNE_CASE_SPEED: // Print speed
        checkkey = PrintSpeed;
        HMI_ValueStruct.print_speed = feedrate_percentage;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(TUNE_CASE_SPEED + MROWS - index_tune), HMI_ValueStruct.print_speed);
        EncoderRate.enabled = true;
        break;
      #if HAS_HOTEND
        case TUNE_CASE_TEMP: // Nozzle temp
          checkkey = ETemp;
          HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(TUNE_CASE_TEMP + MROWS - index_tune), HMI_ValueStruct.E_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_HEATED_BED
        case TUNE_CASE_BED: // Bed temp
          checkkey = BedTemp;
          HMI_ValueStruct.Bed_Temp = thermalManager.degTargetBed();
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(TUNE_CASE_BED + MROWS - index_tune), HMI_ValueStruct.Bed_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_FAN
        case TUNE_CASE_FAN: // Fan speed
          checkkey = FanSpeed;
          HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(TUNE_CASE_FAN + MROWS - index_tune), HMI_ValueStruct.Fan_speed);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_ZOFFSET_ITEM
        case TUNE_CASE_ZOFF: // Z-offset
          #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
            checkkey = Homeoffset;
            HMI_ValueStruct.offset_value = BABY_Z_VAR * 100;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, 202, MBASE(TUNE_CASE_ZOFF + MROWS - index_tune), HMI_ValueStruct.offset_value);
            EncoderRate.enabled = true;
          #else
            // Apply workspace offset, making the current position 0,0,0
            queue.inject_P(PSTR("G92 X0 Y0 Z0"));
            HMI_AudioFeedback();
          #endif
        break;
      #endif
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_PREHEAT

  /* PLA Preheat */
  void HMI_PLAPreheatSetting() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_PLA.inc(1 + PREHEAT_CASE_TOTAL)) Move_Highlight(1, select_PLA.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_PLA.dec()) Move_Highlight(-1, select_PLA.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_PLA.now) {
        case 0: // Back
          checkkey = TemperatureID;
          select_temp.now = TEMP_CASE_PLA;
          HMI_ValueStruct.show_mode = -1;
          Draw_Temperature_Menu();
          break;
        #if HAS_HOTEND
          case PREHEAT_CASE_TEMP: // Nozzle temperature
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = ui.material_preset[0].hotend_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_TEMP), ui.material_preset[0].hotend_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_HEATED_BED
          case PREHEAT_CASE_BED: // Bed temperature
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = ui.material_preset[0].bed_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_BED), ui.material_preset[0].bed_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_FAN
          case PREHEAT_CASE_FAN: // Fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = ui.material_preset[0].fan_speed;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_FAN), ui.material_preset[0].fan_speed);
            EncoderRate.enabled = true;
            break;
        #endif
        #if ENABLED(EEPROM_SETTINGS)
          case 4: { // Save PLA configuration
            const bool success = settings.save();
            HMI_AudioFeedback(success);
          } break;
        #endif
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

  /* ABS Preheat */
  void HMI_ABSPreheatSetting() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_ABS.inc(1 + PREHEAT_CASE_TOTAL)) Move_Highlight(1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_ABS.dec()) Move_Highlight(-1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_ABS.now) {
        case 0: // Back
          checkkey = TemperatureID;
          select_temp.now = TEMP_CASE_ABS;
          HMI_ValueStruct.show_mode = -1;
          Draw_Temperature_Menu();
          break;
        #if HAS_HOTEND
          case PREHEAT_CASE_TEMP: // Set nozzle temperature
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = ui.material_preset[1].hotend_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_TEMP), ui.material_preset[1].hotend_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_HEATED_BED
          case PREHEAT_CASE_BED: // Set bed temperature
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = ui.material_preset[1].bed_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_BED), ui.material_preset[1].bed_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_FAN
          case PREHEAT_CASE_FAN: // Set fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = ui.material_preset[1].fan_speed;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_FAN), ui.material_preset[1].fan_speed);
            EncoderRate.enabled = true;
            break;
        #endif
        #if ENABLED(EEPROM_SETTINGS)
          case PREHEAT_CASE_SAVE: { // Save ABS configuration
            const bool success = settings.save();
            HMI_AudioFeedback(success);
          } break;
        #endif
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

#endif

/* Max Speed */
void HMI_MaxSpeed() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_speed.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_speed.dec()) Move_Highlight(-1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_speed.now, 1, 4)) {
      checkkey = MaxSpeed_value;
      HMI_flag.feedspeed_axis = AxisEnum(select_speed.now - 1);
      HMI_ValueStruct.Max_Feedspeed = planner.settings.max_feedrate_mm_s[HMI_flag.feedspeed_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
      EncoderRate.enabled = true;
    }
    else { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_RATE;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

/* Max Acceleration */
void HMI_MaxAcceleration() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_acc.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_acc.dec()) Move_Highlight(-1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_acc.now, 1, 4)) {
      checkkey = MaxAcceleration_value;
      HMI_flag.acc_axis = AxisEnum(select_acc.now - 1);
      HMI_ValueStruct.Max_Acceleration = planner.settings.max_acceleration_mm_per_s2[HMI_flag.acc_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
      EncoderRate.enabled = true;
    }
    else { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_ACCEL;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_CLASSIC_JERK
  /* Max Jerk */
  void HMI_MaxJerk() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_jerk.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_jerk.dec()) Move_Highlight(-1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) 
    {
      if (WITHIN(select_jerk.now, 1, 4)) 
      {
        checkkey = MaxJerk_value;
        HMI_flag.jerk_axis = AxisEnum(select_jerk.now - 1);
        HMI_ValueStruct.Max_Jerk_scaled = planner.max_jerk[HMI_flag.jerk_axis] * MINUNITMULT;
        //   TO DO
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk_scaled);
        EncoderRate.enabled = true;
      }
      else { // Back
        checkkey = Motion;
        select_motion.now = MOTION_CASE_JERK;
        Draw_Motion_Menu();
      }
    }
    DWIN_UpdateLCD();
  }
#endif // HAS_CLASSIC_JERK

/* Step */
void HMI_Step() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_step.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_step.dec()) Move_Highlight(-1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_step.now, 1, 4)) 
    {
      checkkey = Step_value;
      HMI_flag.step_axis = AxisEnum(select_step.now - 1);    
      HMI_ValueStruct.Max_Step_scaled = planner.settings.axis_steps_per_mm[HMI_flag.step_axis] * MINUNITMULT;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step_scaled);
      EncoderRate.enabled = true;
    }
    else 
    { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_STEPS;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

void HMI_Init() {
  HMI_SDCardInit();

  for (uint16_t t = 0; t <= 100; t += 2) {
    DWIN_ICON_Show(ICON, ICON_Bar, 15, 260);
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 15 + t * 242 / 100, 260, 257, 280);
    DWIN_UpdateLCD();
    delay(20);
  }

  HMI_SetLanguage();
}

void DWIN_Update() 
{
  HMI_SDCardUpdate();       // SD card update
  if(!HMI_flag.disallow_recovery_flag) //在回零过程中不允许拔卡检测和断料检测
  { 
    if(recovery.info.sd_printing_flag) //SD卡打印 
    {
      Remove_card_window_check(); //  rock_20210814
    }
    if(HMI_flag.cloud_printing_flag||card.isPrinting()) //SD卡打印 
    {
      Check_Filament_Update();  /* check filament update */    
    }
  }  
  EachMomentUpdate();       // Status update  
  DWIN_HandleScreen();      // Rotary encoder update
}

void Check_Filament_Update(void)
{
  static uint32_t lMs = millis();
  static uint8_t lNoMatCnt = 0;   
  if((card.isPrinting()|| (!HMI_flag.filement_resume_flag))&& (!temp_cutting_line_flag))
  // if(card.isPrinting() && (!temp_cutting_line_flag) || !HMI_flag.filement_resume_flag)//SD卡打印或者云打印
  {
    if(millis() - lMs > 500)
    {
        lMs = millis();
        /* exist material */
        if(!READ(CHECKFILAMENT_PIN))
        {
            lNoMatCnt = 0;
            return;
        }        
        /* no material */
        else
        {
            lNoMatCnt ++;
        }
        
      if(lNoMatCnt>6)//原为READ(CHECKFILAMENT_PIN) == 0------zy
      {
        lNoMatCnt=0;     
        if(HMI_flag.cloud_printing_flag)
        {          
          HMI_flag.filement_resume_flag=true;  //断料状态挂到，缺料状态
          SERIAL_ECHOLN("M79 S2");
          queue.inject_P(PSTR("M25"));//M25: Pause the SD print in progress.
          print_job_timer.pause(); //云打印暂停
        }
        else  //sd卡打印暂停 
        {
          #if ENABLED(POWER_LOSS_RECOVERY)
            if (recovery.enabled) recovery.save(true, false);  //rock_20211016
          #endif
          queue.inject_P(PSTR("M25"));//M25: Pause the SD print in progress.
        } 
        checkkey = Popup_Window;  
        HMI_flag.cutting_line_flag=true;        
        temp_cutting_line_flag=true;
        Popup_Window_Home();         
      }
    }
  }
}
//拔卡监测
 void Remove_card_window_check(void)
 {
    static bool lSDCardStatus = false;
    /* sd card inserted */
      if(!lSDCardStatus && IS_SD_INSERTED())  //有卡
      {
        lSDCardStatus = IS_SD_INSERTED();        
      }
      /* sd card removed */
      else if(lSDCardStatus && !IS_SD_INSERTED())  //拔卡
      {
          lSDCardStatus = IS_SD_INSERTED();
          /* remove SD card when printing */
          if(IS_SD_PRINTING()&&(!temp_remove_card_flag)) //正在打印  
          {
              checkkey = Popup_Window;
              HMI_flag.remove_card_flag=true;
              temp_remove_card_flag=true;
              Popup_Window_Home();
              #if ENABLED(POWER_LOSS_RECOVERY)
                if (recovery.enabled) recovery.save(true, false);//rock_20211016
              #endif

              queue.inject_P(PSTR("M25"));//M25: Pause the SD print in progress.              
          }
      }
      /* refresh sd card status */
      else
      {
          lSDCardStatus = IS_SD_INSERTED();
      }
}


 
void EachMomentUpdate() 
{
  static float card_Index=0; 
  static millis_t next_var_update_ms = 0, next_rts_update_ms = 0;
  const millis_t ms = millis();
  char *fileName = TERN(POWER_LOSS_RECOVERY, recovery.info.sd_filename, "");
  PrintFile_InfoTypeDef fileInfo = {0};
  if (ELAPSED(ms, next_var_update_ms)) {
    next_var_update_ms = ms + DWIN_VAR_UPDATE_INTERVAL;
    update_variable();
  }
  if (PENDING(ms, next_rts_update_ms)) return;
  next_rts_update_ms = ms + DWIN_SCROLL_UPDATE_INTERVAL;
  if (checkkey == PrintProcess) 
  {
    // if print done
    if(HMI_flag.print_finish && !HMI_flag.done_confirm_flag) 
    {
      HMI_flag.print_finish = false;
      HMI_flag.done_confirm_flag = true;

      TERN_(POWER_LOSS_RECOVERY, recovery.cancel());

      planner.finish_and_disable();

      // show percent bar and value
      _card_percent = 100;
      Draw_Print_ProgressBar();
      
      _remain_time=0;//修复打印完成界面，剩余时间不清零的bug  20211220
      // show print done confirm
      DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 250, DWIN_WIDTH - 1, STATUS_Y);
      DWIN_ICON_Show(ICON, HMI_IsChinese() ? ICON_Confirm_C : ICON_Confirm_E, 86, 283);
    }
    else if (HMI_flag.pause_flag != printingIsPaused()) 
    {
      // print status update
      HMI_flag.pause_flag = printingIsPaused();
      if(!HMI_flag.filement_resume_flag)
      {
        if (HMI_flag.pause_flag) ICON_Continue(); else ICON_Pause();
      }
      
    }
  }

  // pause after homing
  if (HMI_flag.pause_action && printingIsPaused() && !planner.has_blocks_queued()) 
  {
      if(!HMI_flag.cutting_line_flag)
      {
        HMI_flag.pause_action = false;
        #if ENABLED(PAUSE_HEAT)
        TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
        TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());
      //thermalManager.disable_all_heaters();  //rock_20210724 测试部要求暂停仍然在加热
      #endif
      queue.inject_P(PSTR("G1 F1200 X0 Y0"));
      }
  }
// SERIAL_ECHOLNPAIR(" \n\n HMI_flag.cutting_line_flag ", HMI_flag.cutting_line_flag);
// SERIAL_ECHOLNPAIR(" \n\n printingIsPaused() ", printingIsPaused());
// SERIAL_ECHOLNPAIR(" \n\n !planner.has_blocks_queued() ", !planner.has_blocks_queued());
// SERIAL_ECHOLNPAIR(" \n\n checkkey ", checkkey);

 // cutting after homing  || HMI_flag.cloud_printing_flag
  if (HMI_flag.cutting_line_flag && printingIsPaused() && (!planner.has_blocks_queued()||HMI_flag.filement_resume_flag))
  {
    if(!planner.has_blocks_queued())  //防止HMI_flag.filement_resume_flag置1进入，也要继续等待planer为空
    {
        HMI_flag.cutting_line_flag = false;
        #if ENABLED(PAUSE_HEAT)
          TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
          TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());
        #endif  
          queue.inject_P(PSTR("G1 F1200 X0 Y0"));   //rock_20211104 WIFI盒子会发回零指令    
          /* check filament resume */
          if(checkkey == Popup_Window)
          {  
              checkkey = Filament_window;        
              Popup_window_Filament(); 
              HMI_flag.disable_queued_cmd=true; //禁止接收Gcode指令      
              HMI_flag.select_flag=true; //rock_20210917   
          }
    }  
  }
  //联机打印是否暂停
  if(HMI_flag.online_pause_flag&& printingIsPaused() && !planner.has_blocks_queued())
  {
    HMI_flag.online_pause_flag=false;
    queue.inject_P(PSTR("G1 F1200 X0 Y0"));
  }
  
 // cutting after homing
  if (HMI_flag.remove_card_flag && printingIsPaused() && !planner.has_blocks_queued()) 
  {
    //HMI_flag.remove_card_flag = false;
    #if ENABLED(PAUSE_HEAT)
      TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
      TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());

      //thermalManager.disable_all_heaters();  //rock_20210724 测试部要求暂停仍然在加热
    #endif
    queue.inject_P(PSTR("G1 F1200 X0 Y0"));
    /* check filament resume */
    if(checkkey == Popup_Window)
    {  
        checkkey = Remove_card_window;
        Popup_window_Remove_card();     
    }
  }
 //云打印状态更新
  if(HMI_flag.cloud_printing_flag && (checkkey == PrintProcess)&& !HMI_flag.filement_resume_flag)
  {    
    static uint16_t last_Printtime = 0;
    static uint16_t last_card_percent = 0;
    static bool flag=0;
    duration_t elapsed = print_job_timer.duration(); // print timer
    const uint16_t min = (elapsed.value % 3600) / 60;
         //更新进度条
     _card_percent = Cloud_Progress_Bar;
    
    if(last_card_percent!=_card_percent)  //更新APP进度条
    {
      last_card_percent = _card_percent;
      Draw_Print_ProgressBar(); 
      Draw_Print_ProgressRemain();  
    }
    //更新打印时间
    if (last_Printtime != min)
    { // 1 minute update      
      SERIAL_ECHOLNPAIR(" elapsed.value=: ", elapsed.value);
      last_Printtime = min;     
      Draw_Print_ProgressElapsed(); 
      //  _remain_time -= 60;  //剩余时间减去1分钟
      // Draw_Print_ProgressRemain();        
    }
    if(_card_percent<=1 && !flag)   
    {
      flag=true;
      // _remain_time=0; //rock_20210831  解决剩余时间不清零的问题。
      // Draw_Print_ProgressRemain();
    }   
  }

  if (card.isPrinting() && checkkey == PrintProcess) 
  { // print process
    const uint8_t card_pct = card.percentDone();
    //_card_percent=card.percentDone();  //rock_20210901
    static uint8_t last_cardpercentValue = 101;
    if (last_cardpercentValue != card_pct) 
    { // print percent
      last_cardpercentValue = card_pct;
      if (card_pct) 
      {
        _card_percent = card_pct;
        Draw_Print_ProgressBar();
      }
    }
    duration_t elapsed = print_job_timer.duration(); // print timer

    // Print time so far
    static uint16_t last_Printtime = 0;
    const uint16_t min = (elapsed.value % 3600) / 60;
    if (last_Printtime != min) 
    { // 1 minute update
      last_Printtime = min;
      Draw_Print_ProgressElapsed();
    }

    // Estimate remaining time every 20 seconds
    static millis_t next_remain_time_update = 0;
    if (_card_percent > 1 && ELAPSED(ms, next_remain_time_update) && !HMI_flag.heat_flag)   //rock_20210922
    {
      // _remain_time = (elapsed.value - dwin_heat_time) / (_card_percent * 0.01f) - (elapsed.value - dwin_heat_time);
      card_Index=card.getIndex();
      if(card_Index>0) //card_Index = 1;  //rock_20211115 解决偶尔剩余时间显示异常显示>100H 问题
        _remain_time = ((elapsed.value - dwin_heat_time) * ((float)card.getFileSize() / card_Index)) - (elapsed.value - dwin_heat_time);
      else 
      _remain_time =0;
      next_remain_time_update += DWIN_REMAIN_TIME_UPDATE_INTERVAL;
      Draw_Print_ProgressRemain();
    }
    else if(_card_percent<=1 && ELAPSED(ms, next_remain_time_update))   
    {
      _remain_time=0; //rock_20210831  解决剩余时间不清零的问题。
      Draw_Print_ProgressRemain();
    }   
  }
  else if (dwin_abort_flag && !HMI_flag.home_flag) 
  { // Print Stop
    dwin_abort_flag = false;
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
    dwin_zoffset = BABY_Z_VAR;
    select_page.set(0);
    Goto_MainMenu();  //rock_20210831 
  }
  #if ENABLED(POWER_LOSS_RECOVERY)
    else if (DWIN_lcd_sd_status && recovery.dwin_flag) 
    { // resume print before power off
      static bool recovery_flag = false;
      recovery.dwin_flag = false;
      recovery_flag = true;
      auto update_selection = [&](const bool sel) 
      {
        HMI_flag.select_flag = sel;
        const uint16_t c1 = sel ? Color_Bg_Window : Select_Color;
        DWIN_Draw_Rectangle(0, c1, 25, 306, 126, 345);
        DWIN_Draw_Rectangle(0, c1, 24, 305, 127, 346);
        const uint16_t c2 = sel ? Select_Color : Color_Bg_Window;
        DWIN_Draw_Rectangle(0, c2, 145, 306, 246, 345);
        DWIN_Draw_Rectangle(0, c2, 144, 305, 247, 346);
      };
      //BL24CXX::EEPROM_Reset(PLR_ADDR, (uint8_t*)&recovery.info, sizeof(recovery.info));  //rock_20210812  清空 EEPROM
      Popup_Window_Resume();
      update_selection(true);
      get_file_info(&fileName[1],&fileInfo);
      // TODO: Get the name of the current file from someplace
      //
      //(void)recovery.interrupted_file_exists();
      // char * const name = card.longest_filename();
      // const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(name) * (MENU_CHR_W)) / 2;
      // DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, npos, 252, name);
      const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(fileInfo.longfilename) * (MENU_CHR_W)) / 2;
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, npos, 252, fileInfo.longfilename);
      DWIN_UpdateLCD();

      while (recovery_flag) 
      {
        ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
        if (encoder_diffState != ENCODER_DIFF_NO) 
        {
          if (encoder_diffState == ENCODER_DIFF_ENTER) 
          {
            recovery_flag = false;
            if (HMI_flag.select_flag) break;
            TERN_(POWER_LOSS_RECOVERY, queue.inject_P(PSTR("M1000C")));
            HMI_StartFrame(true);
            return;
          }
          else
          {
            update_selection(encoder_diffState == ENCODER_DIFF_CW);
          }
          DWIN_UpdateLCD();
        }
		    HAL_watchdog_refresh();
      }
      select_print.set(0);
      HMI_ValueStruct.show_mode = 0;
      queue.inject_P(PSTR("M1000"));
      Goto_PrintProcess();
      Draw_Status_Area(true);
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, 60, fileInfo.longfilename);
    }
  #endif
  DWIN_UpdateLCD();
  HAL_watchdog_refresh();	
}

void DWIN_HandleScreen() {
  switch (checkkey) {
    case MainMenu:        HMI_MainMenu(); break;
    case SelectFile:      HMI_SelectFile(); break;
    case Prepare:         HMI_Prepare(); break;
    case Control:         HMI_Control(); break;
    case Leveling:        break;
    case PrintProcess:    HMI_Printing(); break;
    case Print_window:    HMI_PauseOrStop(); break;
    case AxisMove:        HMI_AxisMove(); break;
#if ENABLED(HAS_CHECKFILAMENT)
    case Filament_window: HMI_Filament(); break;
#endif
    case Remove_card_window: HMI_Remove_card(); break;
    case TemperatureID:   HMI_Temperature(); break;
    case Motion:          HMI_Motion(); break;
    case AdvSet:          HMI_AdvSet(); break;
    #if HAS_HOME_OFFSET
      case HomeOff:       HMI_HomeOff(); break;
      case HomeOffX:      HMI_HomeOffX(); break;
      case HomeOffY:      HMI_HomeOffY(); break;
      case HomeOffZ:      HMI_HomeOffZ(); break;
    #endif
    #if HAS_ONESTEP_LEVELING
      case ProbeOff:      HMI_ProbeOff(); break;
      case ProbeOffX:     HMI_ProbeOffX(); break;
      case ProbeOffY:     HMI_ProbeOffY(); break;
    #endif
    case Info:            HMI_Info(); break;
    case Tune:            HMI_Tune(); break;
    #if HAS_PREHEAT
      case PLAPreheat:    HMI_PLAPreheatSetting(); break;
      case ABSPreheat:    HMI_ABSPreheatSetting(); break;
    #endif
    case MaxSpeed:        HMI_MaxSpeed(); break;
    case MaxAcceleration: HMI_MaxAcceleration(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk:       HMI_MaxJerk(); break;
    #endif
    case Step:            HMI_Step(); break;
    case Move_X:          HMI_Move_X(); break;
    case Move_Y:          HMI_Move_Y(); break;
    case Move_Z:          HMI_Move_Z(); break;
    #if HAS_HOTEND
      case Extruder:      HMI_Move_E(); break;
      case ETemp:         HMI_ETemp(); break;
    #endif
    #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
      case Homeoffset:    HMI_Zoffset(); break;
    #endif
    #if HAS_HEATED_BED
      case BedTemp:       HMI_BedTemp(); break;
    #endif
    #if HAS_PREHEAT && HAS_FAN
      case FanSpeed:      HMI_FanSpeed(); break;
    #endif
    case PrintSpeed:      HMI_PrintSpeed(); break;
    case MaxSpeed_value:  HMI_MaxFeedspeedXYZE(); break;
    case MaxAcceleration_value: HMI_MaxAccelerationXYZE(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk_value: HMI_MaxJerkXYZE(); break;
    #endif
    case Step_value:      HMI_StepXYZE(); break;
    default: break;
  }
}

void DWIN_CompletedHoming() {
  HMI_flag.home_flag = false;
  dwin_zoffset = TERN0(HAS_BED_PROBE, probe.offset.z);
  if (checkkey == Last_Prepare) {
    checkkey = Prepare;
    select_prepare.now = PREPARE_CASE_HOME;
    index_prepare = MROWS;
    Draw_Prepare_Menu();
  }
  else if (checkkey == Back_Main) {
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
    planner.finish_and_disable();
    Goto_MainMenu();
  }
}

void DWIN_CompletedLeveling() {
  if (checkkey == Leveling) Goto_MainMenu();
}

// void DWIN_StatusChanged(const char *text) {
//   DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, STATUS_Y, DWIN_WIDTH, STATUS_Y + 20);
//   const int8_t x = _MAX(0U, DWIN_WIDTH - strlen_P(text) * MENU_CHR_W) / 2;
//   DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Blue, x, STATUS_Y + 2, F(text));
//   DWIN_UpdateLCD();
// }

// GUI extension
void DWIN_Draw_Checkbox(uint16_t color, uint16_t bcolor, uint16_t x, uint16_t y, bool mode=false) {
  DWIN_Draw_String(false,true,font8x16,Select_Color,bcolor,x+4,y,F(mode ? "x" : " "));
  DWIN_Draw_Rectangle(0,color,x+2,y+2,x+17,y+17);
}

#endif // DWIN_CREALITY_LCD
