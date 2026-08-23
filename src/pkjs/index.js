// API key and units are set from the phone app's Settings page (see
// showConfiguration/webviewclosed below) and persisted in localStorage -
// nothing to edit in this file. Get a free OpenWeatherMap key at
// https://openweathermap.org/api.
var API_KEY = localStorage.getItem("openweather_key") || "";
var UNITS = localStorage.getItem("units") || "metric"; // "metric" = Celsius, "imperial" = Fahrenheit

var KEY_TEMPERATURE = 0;
var KEY_CONDITIONS = 1;
var KEY_WEATHER_ICON = 2;
var KEY_REQUEST_WEATHER = 3;

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
    "<button id='save'>Save</button>" +
    "<script>" +
    "document.getElementById('save').onclick = function() {" +
    "  var settings = {" +
    "    openweather_key: document.getElementById('owkey').value," +
    "    units: document.getElementById('units').value" +
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

  API_KEY = settings.openweather_key || "";
  UNITS = settings.units || "metric";

  console.log("Settings saved from config page");
  getWeather();
});

