// ====== EDIT THIS: paste your free OpenWeatherMap API key here ======
// Get one at https://openweathermap.org/api (free tier is enough)
var API_KEY = "PASTE_YOUR_OPENWEATHERMAP_KEY_HERE";

// "metric" = Celsius, "imperial" = Fahrenheit
var UNITS = "metric";

// ====== EDIT THIS: paste your Anthropic API key here for AI greetings ======
// Get one at https://console.anthropic.com (separate from a claude.ai
// account - this is a developer API key, billed per use, not tied to your
// chat history or memory in any way). Leave the placeholder in place to
// skip this feature entirely; the watch will just use its built-in
// "Hi!"/smile reaction instead.
var ANTHROPIC_API_KEY = "PASTE_YOUR_ANTHROPIC_API_KEY_HERE";
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
  if (!API_KEY || API_KEY.indexOf("PASTE_YOUR") === 0) {
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
  if (!ANTHROPIC_API_KEY || ANTHROPIC_API_KEY.indexOf("PASTE_YOUR") === 0) {
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

