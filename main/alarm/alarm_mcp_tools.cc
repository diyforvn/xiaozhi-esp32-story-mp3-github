// Dang ky cac MCP tool lien quan den alarm: self.alarm.set, self.alarm.list,
// self.alarm.delete. Dung dung API that cua McpServer trong xiaozhi-esp32.
//
// LUU Y VE Property: class Property KHONG co field "description" rieng cho
// tung tham so (xem mcp_server.h that - Property chi co name/type/default/
// min/max, khong co description). Vi vay MOI mo ta y nghia tham so phai
// nam trong MO TA TONG cua ca tool (arg thu 2 cua AddTool), khong duoc
// nhet vao Property() nhu ban truoc (se bi hieu nham thanh default value).

#include "mcp_server.h"
#include "alarm_manager.h"
#include <cinttypes>
#include <string>

void RegisterAlarmMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    // ---- self.alarm.set ----
    mcp_server.AddTool(
        "self.alarm.set",
        "Dat bao thuc / lich nhac viec / lich dieu khien thiet bi vao 1 gio "
        "cu the trong ngay. Tham so: hour (0-23), minute (0-59, bat buoc); "
        "message (noi dung se hien thi khi den gio - PHAI hoi lai nguoi dung "
        "noi dung cu the neu ho chua noi ro, khong duoc de trong neu day la "
        "1 loi nhac thong thuong); repeat (once/daily/weekdays/weekends, "
        "mac dinh once neu khong noi); device_tool (TUY CHON - neu nguoi "
        "dung muon lich nay TU DONG dieu khien 1 thiet bi da co san trong "
        "danh sach tool cua ban, vd 'self.light.turn_on', hay dien dung ten "
        "tool do vao day); device_args (TUY CHON - chuoi JSON tham so cho "
        "device_tool, vd {\"r\":255,\"g\":0,\"b\":0}, de trong {} neu tool "
        "khong can tham so).",
        PropertyList({
            Property("hour", kPropertyTypeInteger, 0, 23),
            Property("minute", kPropertyTypeInteger, 0, 59),
            Property("message", kPropertyTypeString),  // bat buoc, khong default
            Property("repeat", kPropertyTypeString, std::string("once")),
            Property("device_tool", kPropertyTypeString, std::string("")),
            Property("device_args", kPropertyTypeString, std::string("{}")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            Alarm a;
            a.hour = (uint8_t)properties["hour"].value<int>();
            a.minute = (uint8_t)properties["minute"].value<int>();
            a.message = properties["message"].value<std::string>();
            a.repeat = ParseRepeatMode(properties["repeat"].value<std::string>());

            std::string device_tool = properties["device_tool"].value<std::string>();
            if (!device_tool.empty()) {
                a.action = "mcp_tool";
                a.mcp_tool_name = device_tool;
                a.mcp_tool_args = properties["device_args"].value<std::string>();
                if (a.mcp_tool_args.empty()) {
                    a.mcp_tool_args = "{}";
                }
            } else {
                a.action = "notify";
            }
            a.enabled = true;

            uint32_t id = g_alarm_manager.AddAlarm(a);

            char buf[160];
            if (a.action == "mcp_tool") {
                snprintf(buf, sizeof(buf),
                    "Da dat lich luc %02d:%02d de goi %s (id=%" PRIu32 ")",
                    a.hour, a.minute, a.mcp_tool_name.c_str(), id);
            } else {
                snprintf(buf, sizeof(buf), "Da dat bao luc %02d:%02d (id=%" PRIu32 ")",
                         a.hour, a.minute, id);
            }
            return std::string(buf);
        });

    // ---- self.alarm.list ----
    mcp_server.AddTool(
        "self.alarm.list",
        "Liet ke tat ca bao thuc / lich nhac / lich dieu khien thiet bi hien co",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            std::string json = "[";
            bool first = true;
            for (const auto& a : g_alarm_manager.GetAlarms()) {
                if (!first) json += ",";
                first = false;
                char item[384];
                snprintf(item, sizeof(item),
                    "{\"id\":%" PRIu32 ",\"hour\":%u,\"minute\":%u,"
                    "\"repeat\":\"%s\",\"message\":\"%s\",\"action\":\"%s\","
                    "\"device_tool\":\"%s\",\"enabled\":%s}",
                    a.id, a.hour, a.minute, RepeatModeToString(a.repeat),
                    a.message.c_str(), a.action.c_str(),
                    a.mcp_tool_name.c_str(), a.enabled ? "true" : "false");
                json += item;
            }
            json += "]";
            return json;
        });

    // ---- self.alarm.delete ----
    // Chap nhan 2 cach: xoa theo "id" (chinh xac, dung khi da biet id tu
    // self.alarm.list), HOAC xoa theo "hour"+"minute" (tien loi hon cho
    // voice UX). Ca 3 tham so deu co default sentinel -1 (nghia la
    // "khong duoc cung cap"), dung constructor (name, type, default, min, max).
    mcp_server.AddTool(
        "self.alarm.delete",
        "Xoa mot bao thuc/lich. Uu tien dung id neu da biet (tu "
        "self.alarm.list); neu khong biet id, dung hour+minute de xoa "
        "truc tiep theo gio noi ra. Khong cung cap tham so nao thi de "
        "gia tri mac dinh -1.",
        PropertyList({
            Property("id", kPropertyTypeInteger, -1, -1, 2147483647),
            Property("hour", kPropertyTypeInteger, -1, -1, 23),
            Property("minute", kPropertyTypeInteger, -1, -1, 59),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int id_val = properties["id"].value<int>();

            if (id_val >= 0) {
                bool ok = g_alarm_manager.RemoveAlarm((uint32_t)id_val);
                return ok ? std::string("Da xoa bao thuc")
                          : std::string("Khong tim thay bao thuc voi id do");
            }

            int hour_val = properties["hour"].value<int>();
            int minute_val = properties["minute"].value<int>();

            if (hour_val < 0 || minute_val < 0) {
                return std::string(
                    "Can id, hoac ca hour va minute, de xac dinh bao thuc can xoa");
            }

            int idx = g_alarm_manager.FindAlarmIndexByTime((uint8_t)hour_val, (uint8_t)minute_val);
            if (idx < 0) {
                return std::string("Khong tim thay bao thuc luc gio do");
            }

            uint32_t found_id = g_alarm_manager.GetAlarms()[idx].id;
            g_alarm_manager.RemoveAlarm(found_id);
            return std::string("Da xoa bao thuc");
        });
}