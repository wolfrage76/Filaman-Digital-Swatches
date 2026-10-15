#include "timezones.h"

// West to east. The letters in each rule are only zone names; the panel shows
// clock time, never the name. Rows that share a clock still get distinct names
// so the stored rule maps back to the row the user picked.
// Rules follow current law: no DST in Mexico (2022), Iran (2022), Jordan and
// Syria (2022), Fiji (2023) or Paraguay (2024); Kazakhstan on +05 (2024);
// Egypt DST restored (2023).
const TimezoneOption kTimezones[] = {
    {"(UTC-11:00) American Samoa, Midway", "SST11"},
    {"(UTC-10:00) Hawaii", "HST10"},
    {"(UTC-09:00) Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"(UTC-08:00) Pacific Time - US & Canada", "PST8PDT,M3.2.0,M11.1.0"},
    {"(UTC-07:00) Mountain Time - US & Canada", "MST7MDT,M3.2.0,M11.1.0"},
    {"(UTC-07:00) Arizona", "MST7"},
    {"(UTC-06:00) Central Time - US & Canada", "CST6CDT,M3.2.0,M11.1.0"},
    {"(UTC-06:00) Mexico City, Guatemala, Costa Rica", "CST6"},
    {"(UTC-06:00) Saskatchewan", "SKST6"},
    {"(UTC-05:00) Eastern Time - US & Canada", "EST5EDT,M3.2.0,M11.1.0"},
    {"(UTC-05:00) Bogota, Lima, Quito", "COT5"},
    {"(UTC-05:00) Cancun, Panama, Jamaica", "EST5"},
    {"(UTC-04:00) Atlantic Time - Canada, Bermuda", "AST4ADT,M3.2.0,M11.1.0"},
    {"(UTC-04:00) Caracas, La Paz, Manaus", "VET4"},
    {"(UTC-04:00) Puerto Rico, Caribbean", "AST4"},
    {"(UTC-04:00) Santiago", "CLT4CLST,M9.1.6/24,M4.1.6/24"},
    {"(UTC-03:30) Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"(UTC-03:00) Buenos Aires, Sao Paulo, Montevideo", "ART3"},
    {"(UTC-03:00) Asuncion", "PYT3"},
    {"(UTC-02:00) Fernando de Noronha, South Georgia", "FNT2"},
    {"(UTC-01:00) Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1"},
    {"(UTC-01:00) Cape Verde", "CVT1"},
    {"(UTC+00:00) UTC - Coordinated Universal Time", "UTC0"},
    {"(UTC+00:00) London, Dublin", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"(UTC+00:00) Lisbon, Canary Islands", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"(UTC+00:00) Reykjavik, Accra, Dakar", "GMT0"},
    {"(UTC+01:00) Berlin, Paris, Rome, Madrid, Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"(UTC+01:00) Stockholm, Oslo, Copenhagen, Vienna", "MET-1MEST,M3.5.0,M10.5.0/3"},
    {"(UTC+01:00) Warsaw, Prague, Budapest, Belgrade", "CEU-1CEUS,M3.5.0,M10.5.0/3"},
    {"(UTC+01:00) Lagos, Kinshasa, Algiers, Tunis", "WAT-1"},
    {"(UTC+02:00) Athens, Bucharest, Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"(UTC+02:00) Helsinki, Kyiv, Riga, Tallinn, Vilnius", "FLE-2FLES,M3.5.0/3,M10.5.0/4"},
    {"(UTC+02:00) Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"(UTC+02:00) Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"(UTC+02:00) Johannesburg, Harare, Maputo", "SAST-2"},
    {"(UTC+03:00) Istanbul, Amman, Damascus", "TRT-3"},
    {"(UTC+03:00) Moscow, Minsk", "MSK-3"},
    {"(UTC+03:00) Riyadh, Baghdad, Kuwait, Doha", "AST-3"},
    {"(UTC+03:00) Nairobi, Addis Ababa", "EAT-3"},
    {"(UTC+03:30) Tehran", "IRST-3:30"},
    {"(UTC+04:00) Dubai, Muscat, Baku, Tbilisi, Yerevan", "GST-4"},
    {"(UTC+04:30) Kabul", "AFT-4:30"},
    {"(UTC+05:00) Karachi, Tashkent, Almaty", "PKT-5"},
    {"(UTC+05:30) India, Sri Lanka", "IST-5:30"},
    {"(UTC+05:45) Kathmandu", "NPT-5:45"},
    {"(UTC+06:00) Dhaka, Bishkek", "BDT-6"},
    {"(UTC+06:30) Yangon", "MMT-6:30"},
    {"(UTC+07:00) Bangkok, Hanoi, Jakarta", "ICT-7"},
    {"(UTC+08:00) Beijing, Hong Kong, Taipei", "CST-8"},
    {"(UTC+08:00) Singapore, Kuala Lumpur, Manila", "SGT-8"},
    {"(UTC+08:00) Perth", "AWST-8"},
    {"(UTC+09:00) Tokyo, Seoul", "JST-9"},
    {"(UTC+09:30) Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"(UTC+09:30) Darwin", "ACST-9:30"},
    {"(UTC+10:00) Sydney, Melbourne, Canberra, Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"(UTC+10:00) Brisbane", "AEST-10"},
    {"(UTC+10:00) Guam, Port Moresby, Vladivostok", "CHST-10"},
    {"(UTC+11:00) Solomon Islands, New Caledonia", "SBT-11"},
    {"(UTC+12:00) Auckland, Wellington", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"(UTC+12:00) Fiji", "FJT-12"},
    {"(UTC+13:00) Tonga, Samoa", "TOT-13"},
};

const int kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);

int timezone_index(const String &posix) {
  for (int i = 0; i < kTimezoneCount; ++i) {
    if (posix == kTimezones[i].posix) {
      return i;
    }
  }
  return -1;
}
