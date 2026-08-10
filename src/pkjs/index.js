// API keys and units are set from the phone app's Settings page (see
// showConfiguration/webviewclosed below) and persisted in localStorage -
// nothing to edit in this file. Get a free OpenWeatherMap key at
// https://openweathermap.org/api and an Anthropic developer key (separate
// from a claude.ai account, billed per use, no chat history/memory) at
// https://console.anthropic.com. Leaving the Anthropic key blank just means
// the watch uses its built-in "Hi!"/smile reaction instead of an AI one.
var API_KEY = localStorage.getItem("openweather_key") || "";
var UNITS = localStorage.getItem("units") || "metric"; // "metric" = Celsius, "imperial" = Fahrenheit
var ANTHROPIC_API_KEY = localStorage.getItem("anthropic_key") || "";
var ANTHROPIC_MODEL = "claude-haiku-4-5-20251001"; // small & cheap, plenty for a one-line greeting

var KEY_TEMPERATURE = 0;
var KEY_CONDITIONS = 1;
var KEY_WEATHER_ICON = 2;
var KEY_REQUEST_WEATHER = 3;
var KEY_REQUEST_GREETING = 4;
var KEY_AI_TEXT = 5;
var KEY_STEPS = 6;
var KEY_HOUR = 7;

// Remembers the last weather reading so the AI greeting can reference it
// without triggering a second weather fetch.
var lastTemperature = null;
var lastConditions = "";

function locationSuccess(pos) {
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  var url = "https://api.openweathermap.org/data/2.5/weather?lat=" + lat +
            "&lon=" + lon + "&units=" + UNITS + "&appid=" + API_KEY;

  xhrRequest(url, "GET", function(responseText) {
    var json = JSON.parse(responseText);

    if (json.cod && json.cod !== 200) {
      console.log("Weather API error: " + JSON.stringify(json));
      return;
    }

    var temperature = Math.round(json.main.temp);
    var conditions = json.weather && json.weather[0] ? json.weather[0].main : "";
    lastTemperature = temperature;
    lastConditions = conditions;

    var dict = {};
    dict[KEY_TEMPERATURE] = temperature;
    dict[KEY_CONDITIONS] = conditions;

    Pebble.sendAppMessage(dict,
      function() { console.log("Weather sent to watch"); },
      function(e) { console.log("Error sending weather: " + JSON.stringify(e)); }
    );
  });
}

function locationError(err) {
  console.log("Location error: " + JSON.stringify(err));
}

function getWeather() {
  if (!API_KEY) {
    console.log("No OpenWeatherMap API key set yet - skipping weather fetch.");
    return;
  }
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: 60000 }
  );
}

// Asks the Anthropic API for a short, situational greeting when the watch
// double-tap is detected. If no key is configured, or the request fails
// or times out, we simply don't reply - the watch has its own timeout and
// falls back to the built-in "Hi!"/smile reaction on its own, so there's
// nothing to handle here on failure.
function requestAiGreeting(steps, hour) {
  if (!ANTHROPIC_API_KEY) {
    console.log("No Anthropic API key set - watch will use its built-in greeting.");
    return;
  }

  var weatherDesc = lastConditions
    ? (lastConditions + (lastTemperature !== null ? ", " + lastTemperature + "\u00b0" : ""))
    : "unknown";

  var prompt = "You are a tiny friendly robot that lives on someone's smartwatch. " +
    "They just double-tapped you to say hi. Reply with ONE short, warm, playful " +
    "greeting under 30 characters. No quotes, no emoji, no hashtags, no explanation - " +
    "just the greeting itself. You may nod to the context below if it fits naturally, " +
    "but don't force it. Context: hour " + hour + " (24h), weather " + weatherDesc +
    ", steps today " + steps + ".";

  var body = JSON.stringify({
    model: ANTHROPIC_MODEL,
    max_tokens: 20,
    messages: [{ role: "user", content: prompt }]
  });

  var xhr = new XMLHttpRequest();
  xhr.timeout = 3500;

  xhr.onload = function() {
    try {
      var resp = JSON.parse(xhr.responseText);
      var text = (resp.content && resp.content[0] && resp.content[0].text) ? resp.content[0].text : "";
      text = text.replace(/["\n\r]/g, "").trim();
      if (text.length > 40) text = text.substring(0, 40);

      if (text) {
        var dict = {};
        dict[KEY_AI_TEXT] = text;
        Pebble.sendAppMessage(dict,
          function() { console.log("AI greeting sent to watch: " + text); },
          function(e) { console.log("Error sending AI greeting: " + JSON.stringify(e)); }
        );
      } else {
        console.log("AI greeting response had no usable text");
      }
    } catch (e) {
      console.log("AI greeting parse error: " + e);
    }
  };
  xhr.onerror = function() { console.log("AI greeting request failed"); };
  xhr.ontimeout = function() { console.log("AI greeting request timed out"); };

  xhr.open("POST", "https://api.anthropic.com/v1/messages");
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.setRequestHeader("x-api-key", ANTHROPIC_API_KEY);
  xhr.setRequestHeader("anthropic-version", "2023-06-01");
  xhr.send(body);
}

function xhrRequest(url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
}

function escapeHtml(s) {
  return String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;")
    .replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function buildConfigPageUrl() {
  var html = "<!DOCTYPE html><html><head><meta name='viewport' " +
    "content='width=device-width,initial-scale=1'>" +
    "<style>" +
    "body{font-family:sans-serif;background:#0C1012;color:#fff;padding:16px}" +
    "label{display:block;margin-top:16px;font-size:14px;color:#8fd6e8}" +
    "input,select{width:100%;box-sizing:border-box;padding:8px;margin-top:4px;" +
    "font-size:16px;border-radius:4px;border:1px solid #444;background:#1c2226;color:#fff}" +
    "button{margin-top:24px;width:100%;padding:12px;font-size:16px;border:none;" +
    "border-radius:4px;background:#00b3c6;color:#000;font-weight:bold}" +
    "p{font-size:12px;color:#999}" +
    "</style></head><body>" +
    "<h2>Robi Settings</h2>" +
    "<label>OpenWeatherMap API key</label>" +
    "<input id='owkey' type='text' value='" + escapeHtml(API_KEY) + "' " +
    "placeholder='leave blank to disable weather'>" +
    "<p>Free key at openweathermap.org/api</p>" +
    "<label>Units</label>" +
    "<select id='units'>" +
    "<option value='metric'" + (UNITS === "metric" ? " selected" : "") + ">Celsius</option>" +
    "<option value='imperial'" + (UNITS === "imperial" ? " selected" : "") + ">Fahrenheit</option>" +
    "</select>" +
    "<label>Anthropic API key</label>" +
    "<input id='aikey' type='text' value='" + escapeHtml(ANTHROPIC_API_KEY) + "' " +
    "placeholder='leave blank to skip AI greetings'>" +
    "<p>Developer key at console.anthropic.com (not your claude.ai login)</p>" +
    "<button id='save'>Save</button>" +
    "<script>" +
    "document.getElementById('save').onclick = function() {" +
    "  var settings = {" +
    "    openweather_key: document.getElementById('owkey').value," +
    "    units: document.getElementById('units').value," +
    "    anthropic_key: document.getElementById('aikey').value" +
    "  };" +
    "  document.location = 'pebblejs://close#' + encodeURIComponent(JSON.stringify(settings));" +
    "};" +
    "</script></body></html>";

  return "data:text/html;charset=utf-8," + encodeURIComponent(html);
}

Pebble.addEventListener("ready", function() {
  console.log("PebbleKit JS ready");
  getWeather();
});

Pebble.addEventListener("appmessage", function(e) {
  if (e.payload[KEY_REQUEST_WEATHER] !== undefined) {
    getWeather();
  }
  if (e.payload[KEY_REQUEST_GREETING] !== undefined) {
    var steps = e.payload[KEY_STEPS] !== undefined ? e.payload[KEY_STEPS] : 0;
    var hour = e.payload[KEY_HOUR] !== undefined ? e.payload[KEY_HOUR] : new Date().getHours();
    requestAiGreeting(steps, hour);
  }
});

Pebble.addEventListener("showConfiguration", function() {
  Pebble.openURL(buildConfigPageUrl());
});

Pebble.addEventListener("webviewclosed", function(e) {
  if (!e.response) {
    return; // user backed out without saving
  }
  var settings;
  try {
    settings = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log("Could not parse config response: " + err);
    return;
  }

  localStorage.setItem("openweather_key", settings.openweather_key || "");
  localStorage.setItem("units", settings.units || "metric");
  localStorage.setItem("anthropic_key", settings.anthropic_key || "");

  API_KEY = settings.openweather_key || "";
  UNITS = settings.units || "metric";
  ANTHROPIC_API_KEY = settings.anthropic_key || "";

  console.log("Settings saved from config page");
  getWeather();
});

