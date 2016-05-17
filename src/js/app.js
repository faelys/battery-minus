/*
 * Copyright (c) 2016, Natacha Porté
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

var cfg_endpoint = null;
var cfg_data_field = null;
var cfg_sign_algo = "";
var cfg_sign_field = "";
var cfg_sign_field_format = "";
var cfg_sign_key = "";
var cfg_sign_key_format = "";
var cfg_extra_fields = [];

var to_send = [];
var senders = [new XMLHttpRequest(), new XMLHttpRequest()];
var i_sender = 1;
var jsSHA = require("sha");

function sendPayload(payload) {
   var data = new FormData();
   data.append(cfg_data_field, payload);

   if (cfg_sign_field) {
      var sha = new jsSHA(cfg_sign_algo, "TEXT");
      sha.setHMACKey(cfg_sign_key, cfg_sign_key_format);
      sha.update(payload);
      data.append(cfg_sign_field, sha.getHMAC(cfg_sign_field_format));
   }

   if (cfg_extra_fields.length > 0) {
      for (var i = 0; i < cfg_extra_fields.length; i += 1) {
         var decoded = decodeURIComponent(cfg_extra_fields[i]).split("=");
         var name = decoded.shift();
         var value = decoded.join("=");
         data.append(name, value);
      }
   }

   i_sender = 1 - i_sender;
   senders[i_sender].open("POST", cfg_endpoint, true);
   senders[i_sender].send(data);
}

function sendHead() {
   if (to_send.length < 1) return;
   sendPayload(to_send[0].split(";")[1]);
}

function enqueue(key, line) {
   to_send.push(key + ";" + line);
   localStorage.setItem("toSend", to_send.join("|"));
   if (to_send.length === 1) {
      sendHead();
   }
}

function uploadDone() {
   var sent_key = to_send.shift().split(";")[0];
   localStorage.setItem("toSend", to_send.join("|"));
   localStorage.setItem("lastSent", sent_key);
   sendHead();
}

function uploadError() { console.log(this.statusText); }

senders[0].addEventListener("load", uploadDone);
senders[0].addEventListener("error", uploadError);
senders[1].addEventListener("load", uploadDone);
senders[1].addEventListener("error", uploadError);

Pebble.addEventListener("ready", function(e) {
   console.log("Battery- JS ready");

   var str_to_send = localStorage.getItem("toSend");
   to_send = str_to_send ? str_to_send.split("|") : [];

   var str_extra_fields = localStorage.getItem("extraFields");
   cfg_extra_fields = str_extra_fields ? str_extra_fields.split(",") : [];

   cfg_endpoint = localStorage.getItem("cfgEndpoint");
   cfg_data_field = localStorage.getItem("cfgDataField");
   cfg_sign_algo = localStorage.getItem("cfgSignAlgorithm");
   cfg_sign_field = localStorage.getItem("cfgSignFieldName");
   cfg_sign_field_format = localStorage.getItem("cfgSignFieldFormat");
   cfg_sign_key = localStorage.getItem("cfgSignKey");
   cfg_sign_key_format = localStorage.getItem("cfgSignKeyFormat");

   if (cfg_endpoint && cfg_data_field) {
      Pebble.sendAppMessage({ "lastSent":
       parseInt(localStorage.getItem("lastSent") || "0", 10) });
   }

   if (to_send.length >= 1) {
      sendHead();
   }
});

Pebble.addEventListener("appmessage", function(e) {
   if (e.payload.dataKey && e.payload.dataLine) {
      enqueue(e.payload.dataKey, e.payload.dataLine);
   }
});

Pebble.addEventListener("showConfiguration", function() {
   var settings = "?v=1.0";

   if (cfg_endpoint) {
      settings += "&url=" + encodeURIComponent(cfg_endpoint);
   }

   if (cfg_data_field) {
      settings += "&data_field=" + encodeURIComponent(cfg_data_field);
   }

   if (cfg_sign_field) {
      settings += "&s_algo=" + encodeURIComponent(cfg_sign_algo)
       + "&s_field=" + encodeURIComponent(cfg_sign_field)
       + "&s_fieldf=" + encodeURIComponent(cfg_sign_field_format)
       + "&s_key=" + encodeURIComponent(cfg_sign_key)
       + "&s_keyf=" + encodeURIComponent(cfg_sign_key_format);
   }

   if (cfg_extra_fields.length > 0) {
      settings += "&extra=" + cfg_extra_fields.join(",");
   }

   Pebble.openURL("https://cdn.rawgit.com/faelys/battery-minus/v1.0/config.html" + settings);
});

Pebble.addEventListener("webviewclosed", function(e) {
   var configData = JSON.parse(e.response);
   var wasConfigured = (cfg_endpoint && cfg_data_field);

   if (configData.url) {
      cfg_endpoint = decodeURIComponent(configData.url);
      localStorage.setItem("cfgEndpoint", cfg_endpoint);
   }

   if (configData.dataField) {
      cfg_data_field = configData.dataField;
      localStorage.setItem("cfgDataField", cfg_data_field);
   }

   if (configData.signAlgorithm) {
      cfg_sign_algo = configData.signAlgorithm;
      localStorage.setItem("cfgSignAlgorithm", cfg_sign_algo);
   }

   if (configData.signFieldName) {
      cfg_sign_field = configData.signFieldName;
      localStorage.setItem("cfgSignFieldName", cfg_sign_field);
   }

   if (configData.signFieldFormat) {
      cfg_sign_field_format = configData.signFieldFormat;
      localStorage.setItem("cfgSignFieldFormat", cfg_sign_field_format);
   }

   if (configData.signKey) {
      cfg_sign_key = decodeURIComponent(configData.signKey);
      localStorage.setItem("cfgSignKey", cfg_sign_key);
   }

   if (configData.signKeyFormat) {
      cfg_sign_key_format = configData.signKeyFormat;
      localStorage.setItem("cfgSignKeyFormat", cfg_sign_key_format);
   }

   if (configData.extraFields !== null) {
      cfg_extra_fields = configData.extraFields
       ? configData.extraFields.split(",") : [];
      localStorage.setItem("extraFields", cfg_extra_fields.join(","));
   }

   if (configData.resend) {
      senders[0].abort();
      senders[1].abort();
      localStorage.setItem("toSend", "");
      localStorage.setItem("lastSent", "0");
      to_send = [];
      wasConfigured = false;
   }

   if (!wasConfigured && cfg_endpoint && cfg_data_field) {
      Pebble.sendAppMessage({ "lastSent": 0 });
   }
});
