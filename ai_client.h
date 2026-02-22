#pragma once
#include <Arduino.h>

void ai_begin();
void ai_pollSerial();
String ai_sendMessage(const String& userMessage);
