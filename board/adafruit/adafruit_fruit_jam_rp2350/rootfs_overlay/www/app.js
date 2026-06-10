"use strict";

const FruitJam = (() => {
  const api = "/cgi-bin/fruitjam.cgi";
  const state = {
    pixels: ["#ff0000", "#ffaa00", "#00ff66", "#0088ff", "#aa44ff"],
    busy: false,
    log: []
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));

  function line(text) {
    const now = new Date().toLocaleTimeString();
    state.log.unshift(`[${now}] ${text}`);
    state.log = state.log.slice(0, 24);
    $("#log").textContent = state.log.join("\n");
  }

  function setBusy(value) {
    state.busy = value;
    $$("button").forEach((button) => {
      button.disabled = value;
    });
  }

  async function get(params) {
    const query = new URLSearchParams(params);
    const response = await fetch(`${api}?${query.toString()}`, {
      cache: "no-store"
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
  }

  function setStatus(id, ok, label) {
    const node = $(id);
    node.textContent = label || (ok ? "ok" : "missing");
    node.className = ok ? "ok" : "bad";
  }

  function renderPixels() {
    const grid = $("#pixelGrid");
    grid.innerHTML = "";
    state.pixels.forEach((color, index) => {
      const card = document.createElement("div");
      card.className = "pixel";
      card.innerHTML = `
        <div class="pixel-top">
          <strong>Pixel ${index}</strong>
          <span class="swatch"></span>
        </div>
        <input type="color" value="${color}" aria-label="Pixel ${index} color">
      `;
      const swatch = card.querySelector(".swatch");
      const input = card.querySelector("input");
      swatch.style.background = color;
      input.addEventListener("input", () => {
        state.pixels[index] = input.value;
        swatch.style.background = input.value;
      });
      grid.appendChild(card);
    });
  }

  function renderButtons(buttons) {
    const root = $("#buttons");
    root.innerHTML = "";
    buttons.forEach((button) => {
      const pill = document.createElement("div");
      pill.className = `button-pill${button.pressed ? " pressed" : ""}`;
      pill.innerHTML = `<strong>${button.name}</strong><span>GPIO${button.gpio} ${button.pressed ? "pressed" : "released"}</span>`;
      root.appendChild(pill);
    });
  }

  async function refresh() {
    try {
      const data = await get({ action: "status" });
      setStatus("#berryStatus", data.berry && data.berry.ok, data.berry && data.berry.ok ? "running" : "error");
      setStatus("#neoStatus", data.devices && data.devices.neopixels);
      setStatus("#i2cStatus", data.devices && data.devices.i2c0);
      setStatus("#audioStatus", data.devices && data.devices.audio);
      setStatus("#sdStatus", data.devices && data.devices.sd, data.devices && data.devices.sd ? "mounted" : "missing");
      renderButtons(data.buttons || []);
      line("status refreshed");
    } catch (error) {
      line(`status failed: ${error.message}`);
    }
  }

  async function applyPixels() {
    setBusy(true);
    try {
      const params = { action: "neopixels" };
      state.pixels.forEach((color, index) => {
        params[`c${index}`] = color;
      });
      const data = await get(params);
      $("#pixelResult").textContent = data.ok ? data.message : data.error || data.output || "NeoPixel update failed";
      line(`neopixels: ${JSON.stringify(data)}`);
    } catch (error) {
      $("#pixelResult").textContent = error.message;
      line(`neopixels failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }

  function setPalette(colors) {
    state.pixels = colors.slice(0, 5);
    renderPixels();
  }

  async function scanI2c() {
    setBusy(true);
    try {
      const data = await get({ action: "i2c" });
      const root = $("#i2cDevices");
      root.innerHTML = "";
      (data.devices || []).forEach((addr) => {
        const chip = document.createElement("span");
        chip.className = "chip";
        chip.textContent = addr;
        root.appendChild(chip);
      });
      if (!root.children.length) {
        root.textContent = data.ok ? "No devices found." : data.error || "Scan failed.";
      }
      line(`i2c scan: ${JSON.stringify(data)}`);
    } catch (error) {
      line(`i2c failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }

  async function readAdc() {
    setBusy(true);
    try {
      const data = await get({
        action: "adc",
        channel: $("#adcChannel").value
      });
      $("#adcOutput").textContent = data.output || data.error || "No output.";
      line(`adc: ${JSON.stringify(data)}`);
    } catch (error) {
      $("#adcOutput").textContent = error.message;
      line(`adc failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }

  async function playTune() {
    setBusy(true);
    try {
      const data = await get({
        action: "rtttl",
        song: $("#rtttl").value
      });
      $("#rtttlOutput").textContent = data.output || data.error || "No output.";
      line(`rtttl: ${JSON.stringify(data)}`);
    } catch (error) {
      $("#rtttlOutput").textContent = error.message;
      line(`rtttl failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }

  async function testButton(button) {
    setBusy(true);
    try {
      const data = await get({
        action: "button-test",
        button
      });
      line(`button test ${button}: ${JSON.stringify(data)}`);
      await refresh();
    } catch (error) {
      line(`button test failed: ${error.message}`);
    } finally {
      setBusy(false);
    }
  }

  function bind() {
    renderPixels();
    $("#refresh").addEventListener("click", refresh);
    $("#applyPixels").addEventListener("click", applyPixels);
    $("#rainbow").addEventListener("click", () => setPalette(["#ff0033", "#ffb000", "#00d084", "#0099ff", "#aa55ff"]));
    $("#warm").addEventListener("click", () => setPalette(["#ff2200", "#ff6600", "#ffb000", "#ffd166", "#fff1a8"]));
    $("#clearPixels").addEventListener("click", () => setPalette(["#000000", "#000000", "#000000", "#000000", "#000000"]));
    $("#scanI2c").addEventListener("click", scanI2c);
    $("#readAdc").addEventListener("click", readAdc);
    $("#playTune").addEventListener("click", playTune);
    $("#clearLog").addEventListener("click", () => {
      state.log = [];
      $("#log").textContent = "";
    });
    $$("[data-test-button]").forEach((button) => {
      button.addEventListener("click", () => testButton(button.dataset.testButton));
    });
    refresh();
    scanI2c();
    setInterval(refresh, 2500);
  }

  return { bind };
})();

document.addEventListener("DOMContentLoaded", FruitJam.bind);
