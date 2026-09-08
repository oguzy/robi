/**
 * PebbleKit JS Weather Template
 *
 * Fetches weather data from Open-Meteo API (free, no API key needed)
 * and sends temperature + conditions to the watch via AppMessage.
 *
 * Requires package.json to have:
 *   "capabilities": ["location"],
 *   "messageKeys": ["TEMPERATURE", "CONDITIONS", "REQUEST_WEATHER",
 *                   "SUNRISE_MINUTES", "SUNSET_MINUTES",
 *                   "WIND_SPEED_KMH", "UV_INDEX", "STEP_GOAL",
 *                   "ACCENT_COLOR_HEX", "BIRTHDAY_MONTH", "BIRTHDAY_DAY"],
 *   "enableMultiJS": true
 *
 * Also implements the phone app's config page (step goal, accent color,
 * optional birthday) - a plain hand-rolled HTML form opened via a data:
 * URL rather than a hosted page or the Clay library, so the whole
 * watchface stays a single self-contained project with no external
 * config-page hosting to maintain.
 *
 * Place this file at: src/pkjs/index.js
 */

var xhrRequest = function (url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
};

/**
 * Convert WMO weather codes to short human-readable strings.
 * See: https://open-meteo.com/en/docs (WMO Weather interpretation codes)
 */
function weatherCodeToCondition(code) {
  if (code === 0) return 'Clear';
  if (code <= 3) return 'Cloudy';
  if (code <= 48) return 'Fog';
  if (code <= 55) return 'Drizzle';
  if (code <= 57) return 'Fz. Drizzle';
  if (code <= 65) return 'Rain';
  if (code <= 67) return 'Fz. Rain';
  if (code <= 75) return 'Snow';
  if (code <= 77) return 'Snow Grains';
  if (code <= 82) return 'Showers';
  if (code <= 86) return 'Snow Shwrs';
  if (code === 95) return 'T-Storm';
  if (code <= 99) return 'T-Storm';
  return 'Unknown';
}

// Open-Meteo returns daily sunrise/sunset as local time strings (e.g.
// "2026-09-08T06:32") when timezone=auto is set - pull out just the
// minutes-since-midnight so the watch can compare against its own clock
// without needing to parse dates or handle timezones itself.
function isoTimeToMinutes(isoLocalString) {
  var timePart = isoLocalString.split('T')[1];
  var parts = timePart.split(':');
  return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10);
}

function locationSuccess(pos) {
  // Open-Meteo: free weather API, no key required
  var url = 'https://api.open-meteo.com/v1/forecast?' +
      'latitude=' + pos.coords.latitude +
      '&longitude=' + pos.coords.longitude +
      '&current=temperature_2m,weather_code,wind_speed_10m,uv_index' +
      '&daily=sunrise,sunset&timezone=auto';

  xhrRequest(url, 'GET', function(responseText) {
    var json = JSON.parse(responseText);
    var temperature = Math.round(json.current.temperature_2m);
    var conditions = weatherCodeToCondition(json.current.weather_code);

    var dictionary = {
      'TEMPERATURE': temperature,
      'CONDITIONS': conditions
    };

    if (json.daily && json.daily.sunrise && json.daily.sunset) {
      dictionary['SUNRISE_MINUTES'] = isoTimeToMinutes(json.daily.sunrise[0]);
      dictionary['SUNSET_MINUTES'] = isoTimeToMinutes(json.daily.sunset[0]);
    }
    if (typeof json.current.wind_speed_10m === 'number') {
      dictionary['WIND_SPEED_KMH'] = Math.round(json.current.wind_speed_10m);
    }
    if (typeof json.current.uv_index === 'number') {
      dictionary['UV_INDEX'] = Math.round(json.current.uv_index);
    }

    Pebble.sendAppMessage(dictionary,
      function(e) { console.log('Weather info sent to Pebble successfully!'); },
      function(e) { console.log('Error sending weather info to Pebble!'); }
    );
  });
}

function locationError(err) {
  console.log('Error requesting location!');
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: 60000 }
  );
}

// ---------------- CONFIG PAGE ----------------
var CONFIG_STORAGE_KEY = 'robiConfig';
var DEFAULT_CONFIG = { stepGoal: 10000, accentColor: '#55efef', birthdayMonth: 0, birthdayDay: 0 };

function loadStoredConfig() {
  try {
    var raw = localStorage.getItem(CONFIG_STORAGE_KEY);
    if (raw) {
      var parsed = JSON.parse(raw);
      return {
        stepGoal: parsed.stepGoal || DEFAULT_CONFIG.stepGoal,
        accentColor: parsed.accentColor || DEFAULT_CONFIG.accentColor,
        birthdayMonth: parsed.birthdayMonth || 0,
        birthdayDay: parsed.birthdayDay || 0
      };
    }
  } catch (e) {
    console.log('Error reading stored config: ' + e);
  }
  return DEFAULT_CONFIG;
}

function saveStoredConfig(cfg) {
  try {
    localStorage.setItem(CONFIG_STORAGE_KEY, JSON.stringify(cfg));
  } catch (e) {
    console.log('Error saving config: ' + e);
  }
}

function pad2(n) {
  return (n < 10 ? '0' : '') + n;
}

function hexToInt(hex) {
  return parseInt(hex.replace('#', ''), 16);
}

// A plain HTML form, not a hosted page or Clay - built fresh each time
// with the current settings baked in as default values, since the
// data: URL page and this PKJS runtime don't share localStorage (they're
// different origins) to read current values from directly.
function buildConfigHtml(cfg) {
  var birthdayValue = (cfg.birthdayMonth && cfg.birthdayDay)
      ? ('2000-' + pad2(cfg.birthdayMonth) + '-' + pad2(cfg.birthdayDay))
      : '';

  return '<!doctype html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>Robi Settings</title>' +
    '<style>' +
      'body{font-family:-apple-system,Helvetica,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:20px;}' +
      'h1{font-size:20px;margin:0 0 20px;color:#55efef;}' +
      'label{display:block;margin:16px 0 6px;font-size:14px;color:#ccc;}' +
      'input[type=number],input[type=date]{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border-radius:6px;border:1px solid #444;background:#222;color:#eee;}' +
      'input[type=color]{width:100%;height:44px;border:1px solid #444;border-radius:6px;background:#222;padding:2px;}' +
      '.hint{font-size:12px;color:#888;margin-top:4px;}' +
      'button{margin-top:28px;width:100%;padding:14px;font-size:16px;border:none;border-radius:8px;background:#55efef;color:#111;font-weight:bold;}' +
    '</style></head><body>' +
    '<h1>Robi Settings</h1>' +
    '<form id="f">' +
      '<label>Daily step goal</label>' +
      '<input type="number" id="stepGoal" min="1000" max="50000" step="500" value="' + cfg.stepGoal + '">' +
      '<label>Accent color</label>' +
      '<input type="color" id="accentColor" value="' + cfg.accentColor + '">' +
      '<label>Birthday (optional)</label>' +
      '<input type="date" id="birthday" value="' + birthdayValue + '">' +
      '<div class="hint">Robi wears a party hat and says happy birthday on this day every year. Leave blank to disable.</div>' +
      '<button type="submit">Save</button>' +
    '</form>' +
    '<script>' +
    'document.getElementById("f").addEventListener("submit", function(e) {' +
      'e.preventDefault();' +
      'var stepGoal = parseInt(document.getElementById("stepGoal").value, 10) || ' + DEFAULT_CONFIG.stepGoal + ';' +
      'var accentColor = document.getElementById("accentColor").value;' +
      'var bday = document.getElementById("birthday").value;' +
      'var birthdayMonth = 0, birthdayDay = 0;' +
      'if (bday) {' +
        'var parts = bday.split("-");' +
        'birthdayMonth = parseInt(parts[1], 10);' +
        'birthdayDay = parseInt(parts[2], 10);' +
      '}' +
      'var result = { stepGoal: stepGoal, accentColor: accentColor, birthdayMonth: birthdayMonth, birthdayDay: birthdayDay };' +
      'location.href = "pebble://close#" + encodeURIComponent(JSON.stringify(result));' +
    '});' +
    '</script></body></html>';
}

function sendConfig(cfg) {
  var dictionary = {
    'STEP_GOAL': cfg.stepGoal,
    'ACCENT_COLOR_HEX': hexToInt(cfg.accentColor),
    'BIRTHDAY_MONTH': cfg.birthdayMonth || 0,
    'BIRTHDAY_DAY': cfg.birthdayDay || 0
  };
  Pebble.sendAppMessage(dictionary,
    function(e) { console.log('Config sent to Pebble successfully!'); },
    function(e) { console.log('Error sending config to Pebble!'); }
  );
}

Pebble.addEventListener('showConfiguration', function(e) {
  var html = buildConfigHtml(loadStoredConfig());
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response) return;  // config page was dismissed without saving
  try {
    var cfg = JSON.parse(decodeURIComponent(e.response));
    saveStoredConfig(cfg);
    sendConfig(cfg);
  } catch (err) {
    console.log('Error parsing config response: ' + err);
  }
});

// Fetch weather when JS runtime is ready
Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  getWeather();
  // Resend the phone's cached config too, in case the watch app was
  // reinstalled (losing its own persisted copy) since the last save.
  sendConfig(loadStoredConfig());
});

// Handle weather refresh requests from the watch
Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received!');
  if (e.payload['REQUEST_WEATHER']) {
    getWeather();
  }
});
