"use strict";
var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __copyProps = (to, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to, key) && key !== except)
        __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to;
};
var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

// electron/preload.ts
var preload_exports = {};
module.exports = __toCommonJS(preload_exports);
var import_electron = require("electron");
var bridge = {
  saveSession: (id, json) => import_electron.ipcRenderer.invoke("session:save", id, json),
  loadSession: (id) => import_electron.ipcRenderer.invoke("session:load", id),
  listSessions: () => import_electron.ipcRenderer.invoke("session:list"),
  deleteSession: (id) => import_electron.ipcRenderer.invoke("session:delete", id),
  loadSettings: () => import_electron.ipcRenderer.invoke("settings:load"),
  saveSettings: (json) => import_electron.ipcRenderer.invoke("settings:save", json),
  writeTake: (id, file, sampleRate, bitDepth, channels) => import_electron.ipcRenderer.invoke("take:write", id, file, sampleRate, bitDepth, channels),
  readTake: (id, file) => import_electron.ipcRenderer.invoke("take:read", id, file),
  deleteTake: (id, file) => import_electron.ipcRenderer.invoke("take:delete", id, file),
  exportBundle: (req) => import_electron.ipcRenderer.invoke("export:bundle", req),
  chooseDir: () => import_electron.ipcRenderer.invoke("dialog:chooseDir"),
  showItem: (p) => import_electron.ipcRenderer.invoke("shell:showItem", p)
};
import_electron.contextBridge.exposeInMainWorld("jm", bridge);
