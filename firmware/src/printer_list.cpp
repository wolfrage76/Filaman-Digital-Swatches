#include "printer_list.h"

#include "app_config.h"

namespace {
PrinterOption g_printers[MAX_PRINTER_OPTIONS];
int g_count = 0;
String g_activeId;
PrinterPublishFn g_publish = nullptr;
}  // namespace

void printer_list_set(const PrinterOption *printers, int count, const String &activeId) {
  if (count < 0) {
    count = 0;
  }
  if (count > MAX_PRINTER_OPTIONS) {
    count = MAX_PRINTER_OPTIONS;
  }
  for (int i = 0; i < count; ++i) {
    g_printers[i] = printers[i];
  }
  g_count = count;
  g_activeId = activeId;
}

int printer_list_get(PrinterOption *out, int max, String &activeId) {
  int n = g_count < max ? g_count : max;
  for (int i = 0; i < n; ++i) {
    out[i] = g_printers[i];
  }
  activeId = g_activeId;
  return n;
}

void printer_list_set_publisher(PrinterPublishFn fn) { g_publish = fn; }

void printer_list_publish_selection(const String &id) {
  if (g_publish) {
    g_publish(id);
  }
  g_activeId = id;
  config().setFilamanPrinterId(id.toInt());
  for (int i = 0; i < g_count; ++i) {
    g_printers[i].active = (g_printers[i].id == id);
  }
}
