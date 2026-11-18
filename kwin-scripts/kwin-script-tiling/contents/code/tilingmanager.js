/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2012 Mathias Gottschlag <mgottschlag@gmail.com>
Copyright (C) 2013-2014 Fabian Homborg <FHomborg@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

Qt.include("signal.js");
Qt.include("tile.js");
Qt.include("tilelist.js");
Qt.include("layout.js");
Qt.include("spirallayout.js");
Qt.include("halflayout.js");
Qt.include("bladelayout.js");
Qt.include("i3layout.js");
Qt.include("tiling.js");
Qt.include("tests.js");
Qt.include("util.js");
Qt.include("ignored.js");

/**
 * Class which manages all layouts, connects the various signals and handlers
 * and implements all keyboard shortcuts.
 * @class
 */
function TilingManager(timerResize, timerGeometryChanged) {
    // _ws is captured in signal.js at include time (var _ws = workspace;)
    /**
     * Default layout type which is selected for new layouts.
     */
    this.defaultLayout = HalfLayout;

    /**
     * List of all available layout types.
     */
    this.availableLayouts = [
        I3Layout,
        HalfLayout,
        BladeLayout,
        SpiralLayout/*,
                      ZigZagLayout,
                      ColumnLayout,
                      RowLayout,
                      GridLayout,
                      MaximizedLayout,
                      FloatingLayout*/
    ];
    for (var i = 0; i < this.availableLayouts.length; i++) {
        this.availableLayouts[i].index = i;
    }
    /**
     * Number of desktops in the system.
     */
    this.desktopCount = _ws.desktops;
    /**
     * Number of screens in the system.
     */
    this.screenCount = _ws.numScreens;
    /**
     * Dictionary containing an array for each known activity.
     * The arrays in turn contain a list of layouts for every desktop.
     * Each of the lists has one element per screen.
     * I.e.: {"activity": [desktop][screen]}
     */
    this.layouts = {};
    /**
     * List of all tiles in the system.
     */
    this.tiles = new TileList(timerGeometryChanged);
    /**
     * Current screen, needed to be able to track screen changes.
     */
    this._currentScreen = _ws.activeScreen;
    /**
     * Current desktop, needed to be able to track screen changes.
     */
    this._currentDesktop = _ws.currentDesktop;
    /**
     * True if a user moving operation is in progress.
     */
    this._moving = false;
    /**
     * Whether tiling is active on all desktops
     * This is overridden by per-desktop settings
     */
    this.userActive = _kwin.readConfig("userActive", true);
    /**
     * A signal to be emited on manual layout change
     */
    this.layoutChanged = new Signal();
    /**
     * A signal to be emited on manual layout change
     */
    this.tilingChanged = new Signal();

    this._compacting = false;

    this._timerResize = timerResize;

    // Read layout configuration
    // Format: desktop:layoutname[,...]
    // Negative desktop number deactivates tiling
    this.layoutConfig = [];
    var lC = String(_kwin.readConfig("layouts", "")).replace(/ /g,"").split(",");
    for (var i = 0; i < lC.length; i++) {
        var layout = lC[i].split(":");
        try {
            var desktop = parseInt(layout[0]);
        } catch (err) {
            continue;
        }
        var l = this.defaultLayout;
        for (var j = 0; j < this.availableLayouts.length; j++) {
            if (this.availableLayouts[j].name == layout[1]) {
                l = this.availableLayouts[j];
                break;
            }
        }
        if (desktop < 0) {
            var tiling = false;
            desktop = desktop * -1;
        } else {
            var tiling = true;
        }
        if (desktop == 0) {
            this.defaultLayout = l;
            continue;
        }
        var desktoplayout = {};
        desktoplayout.desktop = desktop - 1;
        desktoplayout.layout = l;
        desktoplayout.tiling = tiling;
        this.layoutConfig.push(desktoplayout);
    }

    this._currentActivity = _ws.currentActivity;
    this._addActivity(this._currentActivity);

    var self = this;
    // Connect the tile list signals so that new tiles are added to the layouts
    this.tiles.tileAdded.connect(function(tile) {
        self._onTileAdded(tile);
    });
    this.tiles.tileRemoved.connect(function(tile) {
        self._onTileRemoved(tile);
    });

    // We use this to track current screen
    this.tiles.activeClientChanged.connect(function(client) {
        self._currentScreen = client.screen;
    });

    var existingClients = _ws.clientList();
    for (var i=0; i<existingClients.length; i++) {
        self.tiles.addClient(existingClients[i]);
    }

    // Provide initial values for this.tiles.focusHistory
    // NOTE: Set twice to make the 'current' and 'previous' values equal
    this.tiles.trackFocusChanges();
    this.tiles.trackFocusChanges(this.tiles.focusHistory.current);

    // Activate the visible layouts
    // Do it after adding the existingClients to prevent unnecessary geometry changes
    this._getLayouts(_ws.currentDesktop, null).forEach(function(layout) {
        layout.activate();
    });

    // Register global callbacks
    _ws.numberDesktopsChanged.connect(function() {
        self._onNumberDesktopsChanged();
    });
    _ws.numberScreensChanged.connect(function() {
        self._onNumberScreensChanged();
    });

    _ws.screenResized.connect(function(screen) {
        self._timerResize.screen = screen;
        self._timerResize.start();
    });
    _ws.currentDesktopChanged.connect(function() {
        self._onCurrentDesktopChanged();
    });
    _ws.clientRemoved.connect(function(client) {
        if (_kwin.readConfig("removeEmptyDesktops", false) && !ignored.isIgnored(client) &&
            self.desktopCount > _kwin.readConfig("minDesktopsToKeep", 1))
        {
            self._removeEmptyDesktops();
        }
    });
    _ws.currentActivityChanged.connect(function(id) {
        self._onCurrentActivityChanged(id);
    });
    _ws.activityRemoved.connect(function(id) {
        self._onActivityRemoved(id);
    });
    // Register keyboard shortcuts
    // KWin versions before 5.8.3 do not have this and will crash if we try to call it
    // So just check if the function is available
    if (_kwin.registerShortcut) {
        _kwin.registerShortcut("TILING: Next Tiling Layout",
                              "TILING: Next Tiling Layout",
                              "Meta+Shift+PgDown",
                              function() {
                                  var currentLayout = self._getCurrentLayoutType();
                                  var nextIndex = (currentLayout.index + 1) % self.availableLayouts.length;
                                  self._switchLayout(_ws.currentDesktop,
                                                     _ws.activeScreen,
                                                     nextIndex);
                                  self._notifyLayoutChanged();
                              });
        _kwin.registerShortcut("TILING: Previous Tiling Layout",
                              "TILING: Previous Tiling Layout",
                              "Meta+Shift+PgUp",
                              function() {
                                  var currentLayout = self._getCurrentLayoutType();
                                  var nextIndex = currentLayout.index - 1;
                                  if (nextIndex < 0) {
                                      nextIndex += self.availableLayouts.length;
                                  }
                                  self._switchLayout(_ws.currentDesktop,
                                                     _ws.activeScreen,
                                                     nextIndex);
                                  self._notifyLayoutChanged();
                              });
        _kwin.registerShortcut("TILING: Toggle Floating",
                              "TILING: Toggle Floating",
                              "Meta+F",
                              function() {
                                  var client = _ws.activeClient;
                                  if (client == null) {
                                      print("No active client");
                                      return;
                                  }
                                  // This can be undefined if the client
                                  // has never been seen before
                                  if (client.tiling_floating
                                     || client.tiling_floating == null) {
                                      client.tiling_floating = false;
                                      self.tiles.addClient(client);
                                  } else {
                                      client.tiling_floating = true;
                                      self.tiles.untileClient(client);
                                  }
                              });
        _kwin.registerShortcut("TILING: Toggle Border for all",
                              "TILING: Toggle Border for all",
                              "",
                              function() {
                                  self.tiles.toggleNoBorder();
                              });
        _kwin.registerShortcut("TILING: Move Window Left",
                              "TILING: Move Window Left",
                              "Meta+Shift+H",
                              function() {
                                  self._moveTile(Direction.Left);
                              });
        _kwin.registerShortcut("TILING: Move Window Right",
                              "TILING: Move Window Right",
                              "Meta+Shift+L",
                              function() {
                                  self._moveTile(Direction.Right);
                              });
        _kwin.registerShortcut("TILING: Move Window Up",
                              "TILING: Move Window Up",
                              "Meta+Shift+K",
                              function() {
                                  self._moveTile(Direction.Up);
                              });
        _kwin.registerShortcut("TILING: Move Window Down",
                              "TILING: Move Window Down",
                              "Meta+Shift+J",
                              function() {
                                  self._moveTile(Direction.Down);
                              });
        _kwin.registerShortcut("TILING: Toggle Tiling",
                              "TILING: Toggle Tiling",
                              "Meta+Shift+f11",
                              function() {
                                  var currentScreen = _ws.activeScreen;
                                  var currentDesktop = _ws.currentDesktop;
                                  self._getLayouts(currentDesktop, currentScreen)[0].toggleUserActive();
                                  self._notifyTilingChanged();
                              });
        _kwin.registerShortcut("TILING: Tile now",
                              "TILING: Tile now",
                              "",
                              function() {
                                  var currentScreen = _ws.activeScreen;
                                  var currentDesktop = _ws.currentDesktop;
                                  var layout = self._getLayouts(currentDesktop, currentScreen)[0];
                                  layout.toggleUserActive();
                                  layout.toggleUserActive();
                              });
        _kwin.registerShortcut("TILING: Swap Window With Master",
                              "TILING: Swap Window With Master",
                              "Meta+Shift+M",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client != null) {
                                          var tile = self.tiles.getTile(client);
                                          if (tile != null) {
                                              var layout = self._getLayouts(tile.getDesktop(), tile.getScreen())[0];
                                              if (layout != null) {
                                                  layout.swapTiles(tile, layout.tiles[0]);
                                              }
                                          }
                                      }
                                  } catch(err) {
                                      print(err, "in swap-window-with-master");
                                  }
                              });
        _kwin.registerShortcut("TILING: Resize Active Window To The Left",
                              "TILING: Resize Active Window To The Left",
                              "Meta+Alt+H",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client == null) {
                                          return;
                                      }
                                      var tile = self.tiles.getTile(client);
                                      if (tile == null) {
                                          return;
                                      }
                                      var screenRectangle = util.getTilingArea(client.screen, client.desktop);
                                      var delta = Math.floor(screenRectangle.width / 20);
                                      var geom = Qt.rect(tile.rectangle.x - delta,
                                                         tile.rectangle.y,
                                                         tile.rectangle.width + delta,
                                                         tile.rectangle.height);
                                      if (geom.x < screenRectangle.x) {
                                          geom.x = screenRectangle.x;
                                          geom.width = geom.width - 2 * delta;
                                      }
                                      self._getLayouts(tile.getDesktop(), tile.getScreen())[0].resizeTileTo(tile, geom);
                                  } catch(err) {
                                      print(err, "in resize-window-to-the-left");
                                  }
                              });
        _kwin.registerShortcut("TILING: Resize Active Window To The Right",
                              "TILING: Resize Active Window To The Right",
                              "Meta+Alt+L",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client == null) {
                                          return;
                                      }
                                      var tile = self.tiles.getTile(client);
                                      if (tile == null) {
                                          return;
                                      }
                                      var screenRectangle = util.getTilingArea(client.screen, client.desktop);
                                      var delta = Math.floor(screenRectangle.width / 20);
                                      var geom = Qt.rect(tile.rectangle.x,
                                                         tile.rectangle.y,
                                                         tile.rectangle.width + delta,
                                                         tile.rectangle.height);
                                      if (geom.x + geom.width > screenRectangle.x + screenRectangle.width) {
                                          geom.x = geom.x + delta;
                                          geom.width = (screenRectangle.x + screenRectangle.width) - geom.x;
                                      }
                                      self._getLayouts(tile.getDesktop(), tile.getScreen())[0].resizeTileTo(tile, geom);
                                  } catch(err) {
                                      print(err, "in resize-window-to-the-left");
                                  }
                              });
        _kwin.registerShortcut("TILING: Resize Active Window To The Top",
                              "TILING: Resize Active Window To The Top",
                              "Meta+Alt+K",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client == null) {
                                          return;
                                      }
                                      var tile = self.tiles.getTile(client);
                                      if (tile == null) {
                                          return;
                                      }
                                      var screenRectangle = util.getTilingArea(client.screen, client.desktop);
                                      var delta = Math.floor(screenRectangle.height / 20);
                                      var geom = Qt.rect(tile.rectangle.x,
                                                         tile.rectangle.y - delta,
                                                         tile.rectangle.width,
                                                         tile.rectangle.height + delta);
                                      if (geom.y < screenRectangle.y) {
                                          geom.y = screenRectangle.y;
                                          geom.height = geom.height - 2 * delta;
                                      }
                                      self._getLayouts(tile.getDesktop(), tile.getScreen())[0].resizeTileTo(tile, geom);
                                  } catch(err) {
                                      print(err, "in resize-window-to-the-left");
                                  }
                              });
        _kwin.registerShortcut("TILING: Resize Active Window To The Bottom",
                              "TILING: Resize Active Window To The Bottom",
                              "Meta+Alt+J",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client == null) {
                                          return;
                                      }
                                      var tile = self.tiles.getTile(client);
                                      if (tile == null) {
                                          return;
                                      }
                                      var screenRectangle = util.getTilingArea(client.screen, client.desktop);
                                      var delta = Math.floor(screenRectangle.height / 20);
                                      var geom = Qt.rect(tile.rectangle.x,
                                                         tile.rectangle.y,
                                                         tile.rectangle.width,
                                                         tile.rectangle.height + delta);
                                      if (geom.y + geom.height > screenRectangle.y + screenRectangle.height) {
                                          geom.y = geom.y + delta;
                                          geom.height = (screenRectangle.y + screenRectangle.height) - geom.y;
                                      }
                                      self._getLayouts(tile.getDesktop(), tile.getScreen())[0].resizeTileTo(tile, geom);
                                  } catch(err) {
                                      print(err, "in resize-window-to-the-left");
                                  }
                              });
        _kwin.registerShortcut("TILING: Increase Number Of Masters",
                              "TILING: Increase Number Of Masters",
                              "Meta+*",
                              function() {
                                  try {
                                      self._getLayouts(self._currentDesktop, self._currentScreen)[0].increaseMaster();
                                  } catch(err) {
                                      print(err, "in Increase-Number-Of-Masters");
                                  }
                              });
        _kwin.registerShortcut("TILING: Decrease Number Of Masters",
                              "TILING: Decrease Number Of Masters",
                              "Meta+_",
                              function() {
                                  try {
                                      self._getLayouts(self._currentDesktop, self._currentScreen)[0].decrementMaster();
                                  } catch(err) {
                                      print(err, "in Decrease-Number-Of-Masters");
                                  }
                              });
        _kwin.registerShortcut("TILING: Focus next tile",
                              "TILING: Focus next tile",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null) {
                                          var client = _ws.activeClient;
                                          if (client != null) {
                                              var tile = self.tiles.getTile(client);
                                          }
                                      }
                                      if (tile != null) {
                                          var index1 = layout.tiles.indexOf(tile);
                                          if (index1 == layout.tiles.length-1) {
                                              var index2 = 0;
                                          } else {
                                              var index2 = index1 + 1;
                                          }
                                          var activateClient = layout.tiles[index2].clients[0];
                                          _ws.activeClient = activateClient;
                                      }
                                  } catch(err) {
                                      print(err, "in focus-next-tile");
                                  }
                              });
        _kwin.registerShortcut("TILING: Focus previous tile",
                              "TILING: Focus previous tile",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null) {
                                          var client = _ws.activeClient;
                                          if (client != null) {
                                              var tile = self.tiles.getTile(client);
                                          }
                                      }
                                      if (tile != null) {
                                          var index1 = layout.tiles.indexOf(tile);
                                          if (index1 == 0) {
                                              var index2 = layout.tiles.length-1;
                                          } else {
                                              var index2 = index1 - 1;
                                          }
                                          var activateClient = layout.tiles[index2].clients[0];
                                          _ws.activeClient = activateClient;
                                      }
                                  } catch(err) {
                                      print(err, "in focus-previous-tile");
                                  }
                              });
        _kwin.registerShortcut("TILING: Swap with next tile",
                              "TILING: Swap with next tile",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null) {
                                          var client = _ws.activeClient;
                                          if (client != null) {
                                              var tile = layout.getTile(client.x, client.y);
                                          }
                                      }
                                      if (tile != null) {
                                          var index1 = layout.tiles.indexOf(tile);
                                          if (index1 == layout.tiles.length-1) {
                                              var index2 = 0;
                                          } else {
                                              var index2 = index1 + 1;
                                          }
                                          layout.swapTiles(tile, layout.tiles[index2]);
                                      }
                                  } catch(err) {
                                      print(err, "in swap-with-next-tile");
                                  }
                              });
        _kwin.registerShortcut("TILING: Swap with previous tile",
                              "TILING: Swap with previous tile",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null) {
                                          var client = _ws.activeClient;
                                          if (client != null) {
                                              var tile = layout.getTile(client.x, client.y);
                                          }
                                      }
                                      if (tile != null) {
                                          var index1 = layout.tiles.indexOf(tile);
                                          if (index1 == 0) {
                                              var index2 = layout.tiles.length-1;
                                          } else {
                                              var index2 = index1 - 1;
                                          }
                                          layout.swapTiles(tile, layout.tiles[index2]);
                                      }
                                  } catch(err) {
                                      print(err, "in swap-with-previous-tile");
                                  }
                              });
        _kwin.registerShortcut("TILING-I3: Set Wrap Horizontal Mode",
                              "TILING-I3: Set Wrap Horizontal Mode",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null && layout.layout.isI3Layout) {
                                          layout.layout.state = 'horizontalWrap';
                                      }

                                  } catch(err) {
                                      print(err, "in i3-layout-set-wrap-horizontal-mode");
                                  }
                              });
        _kwin.registerShortcut("TILING-I3: Set Wrap Vertical Mode",
                              "TILING-I3: Set Wrap Vertical Mode",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null && layout.layout.isI3Layout) {
                                          layout.layout.state = 'verticalWrap';
                                      }

                                  } catch(err) {
                                      print(err, "in i3-layout-set-wrap-vertical-mode");
                                  }
                              });
        _kwin.registerShortcut("TILING-I3: Set Normal Mode",
                              "TILING-I3: Set Normal Mode",
                              "",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null && layout.layout.isI3Layout) {
                                          layout.layout.state = 'normal';
                                      }

                                  } catch(err) {
                                      print(err, "in i3-layout-set-normal-mode");
                                  }
                              });
        _kwin.registerShortcut("TILING: Move Window To New Desktop",
                              "TILING: Move Window To New Desktop",
                              "Meta+Shift+D",
                              function() {
                                  try {
                                      var client = _ws.activeClient;
                                      if (client != null) {
                                          var desktop = 0;
                                          // find first empty desktop
                                          for (var i = 1; i <= self.desktopCount; i++) {
                                              if (self._isDesktopEmpty(i)) {
                                                  desktop = i;
                                                  break;
                                              }
                                          }
                                          // if there is none, create a new one
                                          if (desktop == 0 && self.desktopCount < 20) {
                                              _ws.desktops += 1;
                                              desktop = self.desktopCount;
                                          }
                                          // move the client and activate the desktop
                                          if (desktop > 0) {
                                              client.desktop = desktop;
                                              _ws.currentDesktop = desktop;
                                              _ws.activeClient = client;
                                          }
                                      }
                                  } catch(err) {
                                      print(err, "in move-window-to-new-desktop");
                                  }
                              });
        _kwin.registerShortcut("TILING: Dump Clients",
                              "TILING: Dump Clients",
                              "Meta+Shift+Escape",
                              function() {
                                  try {
                                      self._dumpClients();
                                  } catch(err) {
                                      print(err, "in dump-clients");
                                  }
                              });
        _kwin.registerShortcut("TILING: Cycle Rotations",
                              "TILING: Cycle Rotations",
                              "Meta+Shift+R",
                              function() {
                                  try {
                                      var layout = self._getLayouts(_ws.currentDesktop, _ws.activeScreen)[0];
                                      if (layout != null && layout.layout.supportsRotation) {
                                          var grav = layout.layout.getGravity();
                                          switch (grav) {
                                              case Gravity.Bottom:
                                                  var newGrav = Gravity.Right;
                                                  break;
                                              case Gravity.Right:
                                                  var newGrav = Gravity.Top;
                                                  break;
                                              case Gravity.Top:
                                                  var newGrav = Gravity.Left;
                                                  break;
                                              case Gravity.Left:
                                                  var newGrav = Gravity.Bottom;
                                                  break;
                                          }
                                          layout.layout.setGravity(newGrav);
                                          layout.resetTileSizes();
                                      }
                                  } catch(err) {
                                      print(err, "in cycle-rotations");
                                  }
                              });
    }
    // registerUserActionsMenu(function(client) {
    //     return {
    //         text : "Toggle floating",
    //         triggered: function () {
    //             client.tiling_floating = ! client.tiling_floating;
    //             if (client.tiling_floating == true) {
    //                 self.tiles.untileClient(client);
    //             } else {
    //                 self.tiles.addClient(client);
    //             }
    //         }
    //     };
    // });
};

TilingManager.prototype.resize = function() {
    this._onScreenResized(this._timerResize.screen);
};

TilingManager.prototype._createDefaultLayouts = function(activity, desktop) {
    var screenLayouts = [];
    var layout = this.defaultLayout;
    var tiling = false;
    var userConfig = false;
    var layouts = this.layouts[activity];
    for (var i = 0; i < this.layoutConfig.length; i++) {
        if (this.layoutConfig[i].desktop == desktop) {
            userConfig = true;
            layout = this.layoutConfig[i].layout;
            tiling = this.layoutConfig[i].tiling;
            this.layoutConfig.splice(i,1);
        }
    }
    for (var j = 0; j < this.screenCount; j++) {
        screenLayouts[j] = new Tiling(layout, desktop, j);
        // Either the default is to tile and the desktop hasn't been configured,
        // or the desktop has been set to tile (in which case the default is irrelevant)
        screenLayouts[j].userActive = (this.userActive == true && userConfig == false) || (tiling == true);
    }
    layouts[desktop] = screenLayouts;
};

TilingManager.prototype._getCurrentLayoutType = function() {
    var currentLayout = this._getLayouts(this._currentDesktop, this._currentScreen)[0];
    return currentLayout.layoutType;
};

TilingManager.prototype._onTileAdded = function(tile) {
    // Add tile callbacks which are needed to move the tile between different
    // screens/desktops
    var self = this;
    tile.screenChanged.connect(function(oldScreen, newScreen) {
        self._onTileScreenChanged(tile, oldScreen, newScreen);
    });
    tile.desktopChanged.connect(function(oldDesktop, newDesktop) {
        self._onTileDesktopChanged(tile, oldDesktop, newDesktop);
    });
    tile.activitiesChanged.connect(function() {
        self._onTileActivitiesChanged(tile);
    });
    tile.movingStarted.connect(function() {
        self._onTileMovingStarted(tile);
    });
    tile.movingEnded.connect(function() {
        self._onTileMovingEnded(tile);
    });
    tile.resizingEnded.connect(function() {
        self._onTileResized(tile);
    });
    // Add the tile to the layouts
    if (tile.onAllActivities()) {
        var activities = Object.keys(self.layouts);
    } else {
        var activities = tile.getActivities();
    }
    var tileLayouts = this._getActivityLayouts(activities, tile.getDesktop(), tile.getScreen());
    this._addTileToLayouts(tile, tileLayouts);
};

TilingManager.prototype._addTileToLayouts = function(tile, layouts) {
    var self = this;
    var start = _kwin.readConfig("placement", 0);
    layouts.forEach(function(layout) {
        // For I3Layout start at the end is the only option that makes sense,
        // so we should ignore the configured value.
        if (layout.layout.isI3Layout) {
            layout.addTile(tile, self.tiles.focusHistory.previous);
            return;
        }

        // Let KWin decide
        if (start == 0) {
            x = tile.originalx;
            y = tile.originaly;
        // Start as master
        } else if (start == 1) {
            var master = layout.getMaster();
            if (master != null && master.rectangle != null) {
                x = master.rectangle.x;
                y = master.rectangle.y;
            } else {
                x = tile.originalx;
                y = tile.originaly;
            }
        // Start at the end
        } else {
            layout.addTile(tile, self.tiles.focusHistory.previous);
            return;
        }
        layout.addTile(tile, self.tiles.focusHistory.previous, x, y);
    });
};

TilingManager.prototype._onTileResized = function(tile) {
    var tileLayouts = this._getLayouts(tile.getDesktop(), tile.getScreen());
    tileLayouts.forEach(function(layout) {
        layout.resizeTile(tile);
    });
};

TilingManager.prototype._getMaster = function(screen, desktop) {
    try {
        var layouts = this._getLayouts(desktop, screen);
        if (layouts != null && layouts.length > 0) {
            return layouts[0].getMaster();
        } else {
            print("No layout");
        }
    } catch(err) {
        print(err, "in _getMaster");
    }
};

TilingManager.prototype._onTileRemoved = function(tile) {
    try {
        var activities = Object.keys(this.layouts);
        // Search all layouts rather than routing by tile.getDesktop()/getScreen(),
        // which may be stale after hibernate/wake session restore. Only call
        // removeTile on the layout that actually contains the tile.
        var allLayouts = this._getActivityLayouts(activities, -1, null);
        allLayouts.forEach(function(layout) {
            if (layout && layout.tiles.indexOf(tile) > -1) {
                layout.removeTile(tile);
            }
        });
    } catch(err) {
        print(err, "in TilingManager._onTileRemoved");
    }
};

TilingManager.prototype._onNumberDesktopsChanged = function() {
    var self = this;
    var newDesktopCount = _ws.desktops;
    var onAllDesktops = this.tiles.tiles.filter(function(tile) {
        return tile.desktop == -1;
    });
    // FIXME: Is this needed?
    // Remove tiles from desktops which do not exist any more (we only have to
    // care about tiles shown on all desktops as all others have been moved away
    // from the desktops by kwin before)
    var activities = Object.keys(self.layouts);
    for (var i = newDesktopCount; i < this.desktopCount; i++) {
        onAllDesktops.forEach(function(tile) {
            var layouts = self._getActivityLayouts(activities, i, tile.screen);
            layouts.forEach(function(layout) {
                if (layout.tiles.indexOf(tile) > -1) {
                    layout.removeTile(tile);
                }
            });
        });
    }
    // Add new desktops
    for (var i = this.desktopCount; i < newDesktopCount; i++) {
        for (var a in this.layouts) {
            this._createDefaultLayouts(a, i);
            onAllDesktops.forEach(function(tile) {
                var layouts = self._getActivityLayouts([a], i, tile.screen);
                layouts.forEach(function(layout) {
                    layout.addTile(tile, self.tiles.focusHistory.previous);
                });
            });
        };
    }
    // Remove deleted desktops
    if (this.desktopCount > newDesktopCount) {
        for (var a in this.layouts) {
            self.layouts[a].length = newDesktopCount;
        };
    }
    this.desktopCount = newDesktopCount;
};

TilingManager.prototype._onNumberScreensChanged = function() {
    // Add new screens
    if (this.screenCount < _ws.numScreens) {
        for (var i = 0; i < this.desktopCount; i++) {
            for (var j = this.screenCount; j < _ws.numScreens; j++) {
                for (var a in this.layouts) {
                    this.layouts[a][i][j] = new Tiling(this.defaultLayout, i, j);
                    // Activate the new layout if necessary
                    if (a == this._currentActivity && i == _ws.currentDesktop - 1) {
                        this.layouts[a][i][j].activate();
                    }
                };
            }
        }
    }
    // Remove deleted screens
    if (this.screenCount > _ws.numScreens) {
        for (var i = 0; i < this.desktopCount; i++) {
            for (var a in this.layouts) {
                this.layouts[a][i].length = _ws.numScreens;
            };
        }
    }
    this.screenCount = _ws.numScreens;
};

TilingManager.prototype._onScreenResized = function(screen) {
    if (screen != null) {
        if (screen < this.screenCount) {
            for (var i = 1; i <= this.desktopCount; i++) {
                this._getLayouts(i, screen).forEach(function(layout) {
                    layout.activate();
                });
            }
        }
    } else {
        for (var i = 1; i <= this.desktopCount; i++) {
            for (var screen = 0; screen < this.screenCount; screen++) {
                this._getLayouts(i, screen).forEach(function(layout) {
                    layout.activate();
                });
            }
        }
    }
};

TilingManager.prototype._onTileScreenChanged = function(tile, oldScreen, newScreen) {
    if (oldScreen == newScreen) {
        return;
    }
    // If a tile is moved by the user, screen changes are handled in the move
    // callbacks below
    if (this._moving) {
        return;
    }
    // Use tile desktop for the onAllDesktops case
    var desktop = tile.getDesktop();
    var activities = Object.keys(this.layouts);
    var oldLayouts = this._getActivityLayouts(activities, desktop, oldScreen);
    var newLayouts = this._getActivityLayouts(activities, desktop, newScreen);
    this._changeTileLayouts(tile, oldLayouts, newLayouts);
};

TilingManager.prototype._onTileDesktopChanged = function(tile, oldDesktop, newDesktop) {
        try {
            if (oldDesktop == newDesktop || this._compacting) {
                return;
            }
            var client = tile.clients[0];
            if (client == null) {
                print("Tile " + tile.tileIndex + " has no client while switching desktops");
                return;
            }
            if (tile.onAllActivities()) {
                var activities = Object.keys(this.layouts);
            } else {
                var activities = tile.getActivities();
            }
            var oldLayouts = this._getActivityLayouts(activities, oldDesktop, client.screen);
            var newLayouts = this._getActivityLayouts(activities, newDesktop, client.screen);
            // We don't need to handle onAllDesktops special here
            // because removing and readding is a noop
            this._changeTileLayouts(tile, oldLayouts, newLayouts);
        } catch(err) {
            print(err, "in TilingManager._onTileDesktopChanged");
        }
    };

TilingManager.prototype._onTileActivitiesChanged = function(tile) {
    try {
        if (tile.onAllActivities()) {
            var activities = Object.keys(this.layouts);
        } else {
            var activities = tile.getActivities();
        }
        var tileScreen = tile.getScreen();
        var tileDesktop = tile.getDesktop();
        var newLayouts = this._getActivityLayouts(activities, tileDesktop, tileScreen);
        // Fix 10 (2026-10-19): look for the tile in EVERY layout (all activities,
        // desktops and screens), not just at its current desktop/screen. After a
        // resume restart the tile's desktop/screen can be stale, and the copy in the
        // previous activity's layout was never found -> permanent hole ("ghost") in
        // that activity's tiling. _changeTileLayouts' own fallback didn't run because
        // the new layout counted as "found".
        var oldLayouts = this._getActivityLayouts(Object.keys(this.layouts), -1, null)
            .filter(function(layout) {
                return layout && layout.tiles.indexOf(tile) > -1;
            });
        this._changeTileLayouts(tile, oldLayouts, newLayouts);
        this._getLayouts(this._currentDesktop, this._currentScreen)[0].activate();
    } catch(err) {
        print(err, "in TilingManager._onTileActivitiesChanged");
    }
};

/**
 * Fix 10 (2026-10-19): remove tiles from an activity's layouts whose window is not
 * on that activity according to KWin's live client.activities (empty = all
 * activities, kept). Also resyncs the tile's cached activity list. Catches ghosts
 * from any path, including windows tiled at startup before KWin knew their
 * activities and activity changes for which no signal reached the tile.
 */
TilingManager.prototype._pruneForeignTiles = function(activityId, desktop) {
    try {
        var layouts = this._getActivityLayouts([activityId], desktop == null ? -1 : desktop, null);
        var pruned = 0;
        for (var i = 0; i < layouts.length; i++) {
            var layout = layouts[i];
            if (!layout) {
                continue;
            }
            var foreign = layout.tiles.filter(function(tile) {
                var client = tile.clients[0];
                if (client == null) {
                    return false;
                }
                var live = Array.from(client.activities);
                return live.length > 0 && live.indexOf(activityId) < 0;
            });
            for (var j = 0; j < foreign.length; j++) {
                var client = foreign[j].clients[0];
                print("TILING: pruning ghost tile", client.resourceClass.toString(),
                      '"' + client.caption + '"', "from activity", activityId,
                      "desktop", layout.desktop, "screen", layout.screen);
                foreign[j]._activities = Array.from(client.activities);
                layout.removeTile(foreign[j]);
                pruned++;
            }
        }
        return pruned;
    } catch(err) {
        print(err, "in TilingManager._pruneForeignTiles");
        return 0;
    }
};

TilingManager.prototype._onTileMovingStarted = function(tile) {
    // NOTE: This supports only one moving window, breaks with multitouch input
    this._moving = true;
    this._movingStartScreen = tile.clients[0].screen;
};

TilingManager.prototype._onTileMovingEnded = function(tile) {
    try {
        var client = tile.clients[0];
        this._moving = false;
        var windowRect = client.geometry;
        if (client.tiling_tileIndex >= 0) {
            if (tile._currentScreen != client.screen) {
                // Transfer the tile from one layout to another layout
                var startLayout =
                    this._getLayouts(this._currentDesktop, tile.getScreen())[0];
                var endLayout = this._getLayouts(this._currentDesktop, client.screen)[0];
                // Guard: tile may not be in expected layout if state is stale
                if (startLayout.tiles.indexOf(tile) > -1) {
                    startLayout.removeTile(tile);
                } else {
                    // Search all layouts for the tile
                    var activities = Object.keys(this.layouts);
                    var allLayouts = this._getActivityLayouts(activities, -1, null);
                    for (var i = 0; i < allLayouts.length; i++) {
                        if (allLayouts[i].tiles.indexOf(tile) > -1) {
                            allLayouts[i].removeTile(tile);
                            break;
                        }
                    }
                }
                endLayout.addTile(tile,
                                  this.tiles.focusHistory.previous,
                                  windowRect.x + windowRect.width / 2,
                                  windowRect.y + windowRect.height / 2);
            } else {
                // Transfer the tile to a different location in the same layout
                var layout = this._getLayouts(this._currentDesktop, client.screen)[0];
                var targetTile = layout.getTile(windowRect.x + windowRect.width / 2,
                                                windowRect.y + windowRect.height / 2);
                // In case no tile is found (e.g. middle of the window is offscreen), move the client back
                if (targetTile == null) {
                    targetTile = tile;
                }
                // swapTiles() works correctly even if tile == targetTile
                layout.swapTiles(tile, targetTile);
            }
        }
    } catch(err) {
        print(err, "in TilingManager._onTileMovingEnded");
    }
};

TilingManager.prototype._changeTileLayouts = function(tile, oldLayouts, newLayouts) {
        try {
            var self = this;
            var removedFromAny = false;
            if (oldLayouts != null) {
                oldLayouts.forEach(function(layout) {
                    if (newLayouts != null) {
                        var inNew = newLayouts.indexOf(layout);
                        if (inNew > -1) {
                            newLayouts.splice(inNew, 1);
                            removedFromAny = true;
                            return;
                        }
                    }
                    if (layout.tiles.indexOf(tile) > -1) {
                        layout.removeTile(tile);
                        removedFromAny = true;
                    }
                });
            }
            // If tile wasn't found in any oldLayout, search all layouts
            // (desktop/screen may be stale after hibernate/wake session restore)
            if (!removedFromAny) {
                var activities = Object.keys(self.layouts);
                var allLayouts = self._getActivityLayouts(activities, -1, null);
                allLayouts.forEach(function(layout) {
                    if (layout && layout.tiles.indexOf(tile) > -1) {
                        if (newLayouts != null && newLayouts.indexOf(layout) > -1) {
                            newLayouts.splice(newLayouts.indexOf(layout), 1);
                        } else {
                            layout.removeTile(tile);
                            removedFromAny = true;
                        }
                    }
                });
            }
            if (newLayouts != null) {
                newLayouts.forEach(function(layout) {
                    layout.tiles.forEach(function(t) {
                        t.unmaximize();
                    });
                    layout.addTile(tile,
                                   self.tiles.focusHistory.previous);
                });
            }
        } catch(err) {
            print(err, "in TilingManager._changeTileLayouts");
        }
    };

TilingManager.prototype._onCurrentDesktopChanged = function() {
    if (this._currentDesktop === _ws.currentDesktop) {
        return;
    }
    var layouts = this._getLayouts(this._currentDesktop, null);
    layouts.forEach(function(layout) {
        if (layout.active) {
            layout.deactivate();
        }
    });
    this._currentDesktop = _ws.currentDesktop;
    this._pruneForeignTiles(this._currentActivity, this._currentDesktop);
    var layouts = this._getLayouts(this._currentDesktop, null);
    layouts.forEach(function(layout) {
        if (! layout.active) {
            layout.activate();
        }
    });
};

TilingManager.prototype._notifyLayoutChanged = function() {
    var tiling = this._getLayouts(this._currentDesktop, this._currentScreen)[0];
    this.layoutChanged.emit(tiling.layout);
}

TilingManager.prototype._notifyTilingChanged = function() {
    var tiling = this._getLayouts(this._currentDesktop, this._currentScreen)[0];
    this.tilingChanged.emit(tiling.userActive);
}

TilingManager.prototype._switchLayout = function(desktop, screen, layoutIndex) {
    // TODO: Show the layout switcher dialog
    var layoutType = this.availableLayouts[layoutIndex];
    this._getLayouts(desktop, screen).forEach(function(layout) {
        layout.setLayoutType(layoutType);
    });
};

TilingManager.prototype._moveTile = function(direction) {
    var client = _ws.activeClient;
    if (client == null) {
        return;
    }
    var activeTile = this.tiles.getTile(client);
    if (activeTile == null) {
        return;
    }
    // If the client is on all desktops, only move it on the current
    var desktop = client.desktop;
    if (desktop == -1 || client.onAllDesktops == true) {
        desktop = this._currentDesktop;
    }
    var layout = this._getLayouts(desktop, this._currentScreen)[0];
    // Add gaps so we don't land in one
    if (direction == Direction.Left) {
        var x = activeTile.rectangle.x - 1 - layout.windowsGapSizeWidth;
        var y = activeTile.rectangle.y + 1;
    } else if (direction == Direction.Right) {
        var x = activeTile.rectangle.x + activeTile.rectangle.width + 1
            + layout.windowsGapSizeWidth;
        var y = activeTile.rectangle.y + 1;
    } else if (direction == Direction.Up) {
        var x = activeTile.rectangle.x + 1;
        var y = activeTile.rectangle.y - 1 - layout.windowsGapSizeHeight;
    } else if (direction == Direction.Down) {
        var x = activeTile.rectangle.x + 1;
        var y = activeTile.rectangle.y + activeTile.rectangle.height + 1
            + layout.windowsGapSizeHeight;
    } else {
        print("Wrong direction in _moveTile");
        return;
    }
    var nextTile = layout.getTile(x, y);
    if (nextTile != null) {
        layout.swapTiles(activeTile, nextTile);
    } else {
        // No tile to swap. Move to next screen in direction
        var targetScreen = util.nextScreenInDirection(this._currentScreen, desktop, direction);
        if (targetScreen != null) {
            var targetRect = _ws.clientArea(_kwin.ScreenArea, targetScreen, desktop);
            client.geometry = targetRect;
        }
    }
};

/*
 * Function to get the layouts for a given desktop/screen (or all desktops, if -1)
 * Always returns an array (which is more consistent and would make it easier to switch to a multiple-layouts-per-desktop model
 * FIXME: Add a function to _set_ the layouts
 */
TilingManager.prototype._getLayouts = function(desktop, screen) {
    return this._getActivityLayouts([this._currentActivity], desktop, screen);
};

TilingManager.prototype._getActivityLayouts = function(activities, desktop, screen) {
    var result = new Array();
    if (desktop == 0) {
        print("Invalid desktop 0");
        return result;
    }
    for (var i = 0; i < activities.length; i++) {
        var a = activities[i];
        var layouts = this.layouts[a];
        if (layouts == null) {
            continue;
        }
        if (desktop == -1) {
            if (screen != null) {
                for (var j = 0; j < this.desktopCount; j++) {
                    result.push(layouts[j][screen]);
                }
            } else {
                for (var j = 0; j < layouts.length; j++) {
                    result = result.concat(layouts[j]);
                }
            }
        } else {
            if (screen != null) {
                result = result.concat([layouts[desktop - 1][screen]]);
            } else {
                result = result.concat(layouts[desktop - 1]);
            }
        }
    }
    return result;
};

TilingManager.prototype._isDesktopEmpty = function(desktop) {
    var clients = _ws.clientList();
    var empty = true;
    for (var i = 0; i < clients.length; i++) {
        if (!clients[i].onAllDesktops && clients[i].desktop == desktop) {
            empty = false;
            break;
        }
    }
    return empty;
};

TilingManager.prototype._compactDesktops = function() {
    this._compacting = true;
    for (var destination = 1; destination < this.desktopCount; destination++) {
        if (!this._isDesktopEmpty(destination)) {
            continue;
        }

        for (var source = destination + 1; source <= this.desktopCount; source++) {
            if (this._isDesktopEmpty(source)) {
                continue;
            }
            var destinationLayouts = this._getLayouts(destination, null);
            var sourceLayouts = this._getLayouts(source, null);
            for (var i = 0; i < destinationLayouts.length; i++) {
                sourceLayouts[i].deactivate();
                destinationLayouts[i].deactivate();
                var oldLayout = destinationLayouts[i];
                destinationLayouts[i] = sourceLayouts[i];
                sourceLayouts[i] = oldLayout;
                destinationLayouts[i].desktop = destination;
                sourceLayouts[i].desktop = source;
            }
            var clients = _ws.clientList();
            for (var i = 0; i < clients.length; i++) {
                var cl = clients[i];
                if (!cl.onAllDesktops && cl.desktop == source) {
                    cl.desktop = destination;
                }
            }
            for (i = 0; i < destinationLayouts.length; i++) {
                sourceLayouts[i].activate();
                destinationLayouts[i].activate();
            }
            break;
        }
    }
    this._compacting = false;
}

TilingManager.prototype._removeEmptyDesktops = function() {
    var clients = _ws.clientList();

    this._compactDesktops();

    // Desktop 0 is a special desktop for unmapped clients
    // Desktop 1 is not a good candidate for removal
    for (var i = this.desktopCount; i > 1; i--) {
        if (this._isDesktopEmpty(i)) {
            _ws.desktops -= 1;
            if (!_ws.activeClient) {
                var master = this._getMaster(this._currentScreen, this._currentDesktop);
                if (master) {
                    var client = master.getActiveClient();
                    if (!client) {
                        client = master.clients[0];
                    }
                    _ws.activeClient = client;
                }
            }
        } else {
            break;
        }
    }
};

TilingManager.prototype._dumpClients = function() {
    // We build up the dump string to only call `print()` once,
    // to make it clear that this is one call.
    // Especiallly in the journal, this would have each line prefixed with time/date, pid and such.
    var outp = "";
    var activities = Object.keys(this.layouts);
    for (var a in activities) {
        var activityId = activities[a];
        outp += "Activity " + activityId + "\n";
        for (var i = 1; i <= this.desktopCount; i++) {
            outp += " Desktop " + i + "\n";
            for(var j = 0; j < this.screenCount; j++) {
                outp += "  Screen " + j + "\n";
                var layout = this._getActivityLayouts([activityId], i, j)[0];
                for (var k = 0; k < layout.tiles.length; k++) {
                    outp += "   Tile " + k + "\n";
                    layout.tiles[k].clients.forEach(function(c) {
                        outp += "     " + c.resourceClass.toString() + '- "' + c.caption + " " + c.keepBelow + " " + c.keepAbove + '"\n';
                    });
                }
            }
        }
    }
    print(outp);
}

TilingManager.prototype._addActivity = function(id) {
    var self = this;
    this.layouts[id] = new Array();
    for (var i = 0; i < this.desktopCount; i++) {
        this._createDefaultLayouts(id, i);
    }
    var onActivity = this.tiles.tiles.filter(function(tile) {
        return tile.onAllActivities() || tile.getActivities().includes(id);
    });
    onActivity.forEach(function(tile) {
        var layouts = self._getActivityLayouts([id], tile.getDesktop(), tile.getScreen());
        layouts.forEach(function(layout) {
            layout.addTile(tile);
        });
    });
};

TilingManager.prototype._onCurrentActivityChanged = function(id) {
    if (this._currentActivity == id) {
        return;
    }
    var layouts = this._getLayouts(-1, null);
    layouts.forEach(function(layout) {
        if (layout.active) {
            layout.deactivate();
        }
    });

    // We are creating layouts for activities lazily. Create a layouts item for
    // the new activity and populate it with default layouts if it does not exist.
    if (!(id in this.layouts)) {
        this._addActivity(id);
    }
    this._currentActivity = id;
    this._pruneForeignTiles(id, null);

    var layouts = this._getLayouts(-1, null);
    layouts.forEach(function(layout) {
        if (!layout.active) {
            layout.activate();
        }
    });
};

TilingManager.prototype._onActivityRemoved = function(id) {
    if (id in this.layouts) {
        delete this.layouts[id];
    }
};
