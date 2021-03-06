<!--
 * Copyright 2020 Florian Kramer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
-->

<template>
  <div id="map">
    <canvas v-on:mousedown="onMouseDown" v-on:mouseup="onMouseUp"
            v-on:mousemove="onMouseMove" v-on:wheel="onMouseWheel"
            v-on:contextmenu.prevent v-on:keydown="onKeyDown"
            tabindex="0"/>
  </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import Server, { ServerState } from './server'
import eventBus from '../eventbus'
import * as Sim from '../simulation/simulation'
import Tool from '../tools/tool'
import ToolToken from '../tools/tool_token'
import ToolLine from '../tools/tool_line'
import ToolRoom from '../tools/tool_room'
import ToolWall from '../tools/tool_wall'
import ToolDoor from '../tools/tool_door'
import ToolFurniture from '../tools/tool_furniture'
import ToolReveal from '../tools/tool_reveal'
import Renderer from '../rendering/renderer'
import GridActor from '../rendering/gridactor'
import TokenActor from '../rendering/token_actor'
import Actor from '../rendering/actor'
import DiffuseMaterial from '../rendering/diffuse_material'
import * as B from '../simulation/building'
import LineActor from '../rendering/lineactor'
import RoomActor from '../rendering/roomactor'
import WallActor from '../rendering/wallactor'
import FurnitureActor from '../rendering/furnitureactor'
import DoorActor from '../rendering/dooractor'
import RenderLayers from './renderlayers'
import TileRenderer from '../rendering/tilerenderer'

enum MouseAction {
  NONE,
  DRAG,
  CREATE_LINE
}

@Component
export default class World extends Vue {
  tokens: Sim.Token[] = []
  movingTokens: Sim.Token[] = []
  lines: Sim.Line[] = []

  canvas?: HTMLCanvasElement
  ctx: WebGLRenderingContext | null = null
  renderer: Renderer = new Renderer()
  gridActor: GridActor = new GridActor()

  tokenActors: Map<number, Actor> = new Map()
  doodadLineActors: Map<number, Actor> = new Map()
  roomActor: RoomActor = new RoomActor()
  wallActor: WallActor = new WallActor()
  furnitureActor: FurnitureActor = new FurnitureActor()
  doorActor: DoorActor = new DoorActor()

  tileRenderer : TileRenderer = new TileRenderer()

  mouseAction: MouseAction = MouseAction.NONE

  lastMouseX: number = 0
  lastMouseY: number = 0

  pixelPerMeter: number = 1

  selected?: Sim.Token

  renderQueued: boolean = false
  lastRender: number = 0

  tool: Tool = new Tool(this)

  currentBuilding?: B.Building = undefined

  lastCanvasWidth: number = 0
  lastCanvasHeight: number = 0

  // True if we already received the server state
  hasState: boolean = false

  mutationObserver : MutationObserver | null = null

  constructor () {
    super()

    eventBus.$on('/server/token/create', (data: Sim.Token) => { this.onNewToken(data) })
    eventBus.$on('/server/token/clear', () => { this.clearTokens() })
    eventBus.$on('/server/token/delete', (data: Sim.Token) => { this.onServerDeleteToken(data) })
    eventBus.$on('/server/token/move', (data: Sim.TokenMoveOrder) => { this.onServerMoveToken(data) })
    eventBus.$on('/server/token/toggle_foe', (data: Sim.Token) => { this.onServerToggleFoe(data) })
    eventBus.$on('/server/line/create', (data: Sim.Line) => { this.onServerCreateLine(data) })
    eventBus.$on('/server/line/clear', () => { this.onServerClearLines() })
    eventBus.$on('/server/state', (data: ServerState) => { this.onServerState(data) })
    eventBus.$on('/server/is_gm', (data: boolean) => { this.onServerIsGm(data) })

    eventBus.$on('/server/building/load', (data: B.Building) => { this.onServerLoadBuilding(data) })
    eventBus.$on('/server/building/clear', () => { this.onServerClearBuilding() })

    eventBus.$on('/server/building/toggle_door', (data: number[]) => { this.onServerToggleDoor(data) })
    eventBus.$on('/server/building/room/create', (data: B.Room) => { this.onServerCreateRoom(data) })
    eventBus.$on('/server/building/room/delete', (data: B.Room) => { this.onServerDeleteRoom(data) })
    eventBus.$on('/server/building/room/modified', (data: B.Room) => { this.onServerModifyRoom(data) })
    eventBus.$on('/server/building/wall/create', (data: B.Wall) => { this.onServerCreateWall(data) })
    eventBus.$on('/server/building/wall/delete', (data: B.Wall) => { this.onServerDeleteWall(data) })
    eventBus.$on('/server/building/wall/modified', (data: B.Wall) => { this.onServerModifyWall(data) })
    eventBus.$on('/server/building/door/create', (data: B.Door) => { this.onServerCreateDoor(data) })
    eventBus.$on('/server/building/door/delete', (data: B.Door) => { this.onServerDeleteDoor(data) })
    eventBus.$on('/server/building/door/modified', (data: B.Door) => { this.onServerModifyDoor(data) })
    eventBus.$on('/server/building/furniture/create', (data: B.Furniture) => { this.onServerCreateFurniture(data) })
    eventBus.$on('/server/building/furniture/delete', (data: B.Furniture) => { this.onServerDeleteFurniture(data) })
    eventBus.$on('/server/building/furniture/modified', (data: B.Furniture) => { this.onServerModifyFurniture(data) })

    eventBus.$on('/server/tiles/set', (path: string) => { this.tileRenderer.loadFrom(path) })
    eventBus.$on('/server/tiles/clear', () => { this.tileRenderer.clear() })

    eventBus.$on('/tools/select_tool', (data: string) => { this.onToolSelected(data) })

    eventBus.$on('/client/colorscheme/changed', () => { this.requestRedraw() })
  }

  requestRedraw (dontResize: boolean = false) {
    if (!dontResize && this.canvas && (this.lastCanvasWidth !== this.$el.clientWidth || this.lastCanvasHeight !== this.$el.clientHeight)) {
      this.onResize(true)
    }
    requestAnimationFrame(this.renderMap)
  }

  screenToWorldPos (sp: Sim.Point) : Sim.Point {
    return this.renderer.camera.screenToWorldSpace(sp)
  }

  worldToScreenPos (sp: Sim.Point) : Sim.Point {
    return this.renderer.camera.worldToScreenSpace(sp)
  }

  renderMap () {
    let now = new Date().getTime()

    if (this.ctx !== null && this.canvas) {
      this.tool.render(this.renderer)
      this.renderer.beginFrame()
      this.renderer.drawFrame()
      this.renderer.endFrame()
    }
    let end: number = Date.now()
  }

  setupScreenSpaceFont (ctx: CanvasRenderingContext2D) {
    ctx.font = '30px sans-serif'
  }

  computeLineWidth () : number {
    return 2 * (this.renderer.camera.height / this.renderer.camera.heightPixels)
  }

  moveByScreenDelta (dx: number, dy: number) {
    if (this.canvas) {
      this.renderer.camera.pan(dx * 2 / this.canvas.width, -dy * 2 / this.canvas.height)
    }
  }

  hasSelection () : boolean {
    return this.selected !== undefined
  }

  getSelection () : Sim.Token | undefined {
    return this.selected
  }

  resetCamera () {
    this.renderer.camera.reset()
    this.requestRedraw()
  }

  setLastMousePos (sx: number, sy: number) {
    this.lastMouseX = sx
    this.lastMouseY = sy
  }

  getLastMousePos () : Sim.Point {
    return new Sim.Point(this.lastMouseX, this.lastMouseY)
  }

  addFurniture (f: B.Furniture) {
    // if (this.currentBuilding === undefined) {
    //   this.currentBuilding = new B.Building()
    // }
    // this.currentBuilding.addFurniture(f)
    // this.requestRedraw()
  }

  addDoor (door: B.Door) {
    // if (this.currentBuilding === undefined) {
    //   this.currentBuilding = new B.Building()
    // }
    // this.currentBuilding.addDoor(door)
    // this.requestRedraw()
  }

  removeFurnitureAt (pos: Sim.Point) {
    // if (this.currentBuilding !== undefined) {
    //   this.currentBuilding.removeFurnitureAt(pos)
    // }
  }

  getFurnitureAt (pos: Sim.Point): B.Furniture | undefined {
    // if (this.currentBuilding !== undefined) {
    //   return this.currentBuilding.getFurnitureAt(pos)
    // }
    return undefined
  }

  removeDoorAt (wx: number, wy: number) {
    // if (this.currentBuilding !== undefined) {
    //   this.currentBuilding.removeDoorAt(wx, wy)
    // }
  }

  canToggleDoorAt (wx: number, wy: number) : boolean {
    // if (this.$store.state.permissions === 1) {
    //   if (this.currentBuilding !== undefined) {
    //     return this.currentBuilding.isDoorAt(wx, wy)
    //   }
    //   return false
    // }
    return false
  }

  getDoorsAt (wx: number, wy: number) : B.Door[] {
    // if (this.currentBuilding !== undefined) {
    //   return this.currentBuilding.getDoorsAt(wx, wy)
    // }
    return []
  }

  getRoomAt (pos: Sim.Point) : B.Room | undefined {
    // if (this.currentBuilding !== undefined) {
    //   return this.currentBuilding.getRoomAt(pos)
    // }
    return undefined
  }

  onServerToggleDoor (doors: number[]) {
    // if (this.currentBuilding !== undefined) {
    //   // TODO: synchronize
    //   let b = this.currentBuilding.toggleDoors(doors)
    //   this.requestRedraw()
    //   return b
    // }
    // return false
  }

  revealRoomsAt (wpos: Sim.Point) {
    // if (this.currentBuilding !== undefined) {
    //   this.currentBuilding.revealRoomsAt(wpos)
    //   eventBus.$emit('/client/building/set', this.currentBuilding)
    // }
  }

  /**
   * @param wx The x coordinate in world space
   * @param wy The y coordinate in world space
   * @returns True if a new token was selected
   */
  selectTokenAt (wx: number, wy: number) : boolean {
    for (let token of this.tokens) {
      if (Math.hypot(token.x - wx, token.y - wy) < token.radius) {
        this.setSelectedToken(token)
        return true
      }
    }
    this.setSelectedToken(undefined)
    return false
  }

  setSelectedToken (selected: Sim.Token | undefined) {
    if (this.selected !== undefined) {
      let a = this.tokenActors.get(this.selected.id) as TokenActor
      if (a) {
        a.setIsSelected(false)
      }
    }
    this.selected = selected
    if (this.selected !== undefined) {
      let a = this.tokenActors.get(this.selected.id) as TokenActor
      if (a) {
        a.setIsSelected(true)
      }
    }
  }

  onKeyDown (event: KeyboardEvent) {
    this.tool.onKeyDown(event)
  }

  onMouseDown (event: MouseEvent) {
    this.tool.onMouseDown(event)
  }

  clientMoveSelectedTo (x: number, y: number, a: number) {
    if (this.selected !== undefined) {
      // move the selected token
      let move = new Sim.TokenMoveOrder()
      move.x = x
      move.y = y
      move.rotation = a
      move.token = this.selected
      eventBus.$emit('/client/token/move', move)
    }
  }

  clientDeleteSelectedToken () {
    if (this.hasSelection()) {
      eventBus.$emit('/client/token/delete', this.selected)
    }
  }

  clientToggleFoeSelectedToken () {
    if (this.hasSelection()) {
      eventBus.$emit('/client/token/toggle_foe', this.selected)
    }
  }

  clientSpawnTokenAt (wx: number, wy: number) {
    eventBus.$emit('/client/token/create', new Sim.Point(wx, wy))
  }

  onMouseMove (event: MouseEvent) {
    this.tool.onMouseMove(event)
  }

  onMouseUp (event: MouseEvent) {
    this.tool.onMouseUp(event)
  }

  onMouseWheel (event: WheelEvent) {
    this.renderer.camera.zoom(event.deltaY)
    this.requestRedraw()
  }

  mounted () {
    console.log('Mounted the canvas')
    // Request the inital state from the server
    eventBus.$emit('/server/request_state')

    this.canvas = this.$el.children[0] as HTMLCanvasElement
    this.canvas.width = this.$el.scrollWidth
    this.canvas.height = this.$el.scrollHeight
    this.pixelPerMeter = this.canvas.height / 10

    this.ctx = this.canvas.getContext('webgl')
    if (this.ctx) {
      this.renderer.gl = this.ctx
      this.renderer.init()
      this.renderer.onResize(this.canvas.width, this.canvas.height)
      this.initActors()
    }

    // Handle window resizes
    window.addEventListener('resize', (ev: UIEvent) => {
      this.onResize()
    })
    document.addEventListener('DockSpawnResizedEvent', () => {
      this.onResize()
    })
    // Observe changes to the style triggered by the tabbed ui
    this.mutationObserver = new MutationObserver((mutations) => {
      mutations.forEach((mutationRecord) => {
        this.onResize()
      })
    })
    let mapContainer = document.getElementById('map')
    if (mapContainer !== null) {
      this.mutationObserver.observe(mapContainer, { attributes: true, attributeFilter: ['style'] })
    } else {
      console.log('Unable to find the map div')
    }

    this.requestRedraw()
  }

  onResize (dontRedraw: boolean = false) {
    this.lastCanvasWidth = this.$el.clientWidth
    this.lastCanvasHeight = this.$el.clientHeight
    if (this.canvas) {
      this.canvas.width = this.$el.clientWidth
      this.canvas.height = this.$el.clientHeight
      this.renderer.onResize(this.canvas.width, this.canvas.height)
      if (!dontRedraw) {
        this.requestRedraw()
      }
      this.pixelPerMeter = this.canvas.height / 10
    }
  }

  initActors () {
    this.renderer.addActor(this.gridActor, RenderLayers.GRID)
    this.renderer.addActor(this.roomActor, RenderLayers.BUILDING)
    this.renderer.addActor(this.doorActor, RenderLayers.DOORS)
    this.renderer.addActor(this.wallActor, RenderLayers.BUILDING)
    this.renderer.addActor(this.furnitureActor, RenderLayers.BUILDING)

    this.tileRenderer.setRenderCallback(this.requestRedraw)
    this.tileRenderer.init(this.renderer)
  }

  updateMovingTokens () {
    let delta = 0.016
    let start: number = Date.now()
    let toRemove : Sim.Token[] = []
    this.movingTokens.forEach(t => {
      let a = this.tokenActors.get(t.id)

      let dx = t.x - t.displayX
      let dy = t.y - t.displayY
      let l = Math.hypot(dx, dy)
      if (l < 1.1 * t.displaySpeed * delta) {
        t.displayX = t.x
        t.displayY = t.y
        if (a) {
          a.setRotation(t.rotation)
        }
        toRemove.push(t)
      } else {
        t.displayX += delta * t.displaySpeed * dx / l
        t.displayY += delta * t.displaySpeed * dy / l
      }
      if (a) {
        a.setPosition(t.displayX, t.displayY)
      }
    })

    toRemove.forEach(t => {
      this.movingTokens.splice(this.movingTokens.indexOf(t), 1)
    })

    this.requestRedraw()

    if (this.movingTokens.length > 0) {
      let stop: number = Date.now()
      let timeTaken = (stop - start)
      let sleep = Math.max(0, (1000 * delta) - timeTaken)
      setTimeout(this.updateMovingTokens, sleep)
    }
  }

  onNewDoodadLine (line: Sim.Line) {
    this.lines.push(line)
    this.createDoodadLineActor(line)
    this.requestRedraw()
  }

  createDoodadLineActor (line: Sim.Line) {
    let l = new LineActor()
    l.setLine(line.start.x, line.start.y, line.stop.x, line.stop.y, 0.1)
    this.renderer.addActor(l, RenderLayers.DOODADS)
    this.doodadLineActors.set(line.id, l)
  }

  onNewToken (data: Sim.Token) {
    data.displayX = data.x
    data.displayY = data.y
    this.tokens.push(data)

    this.createTokenActor(data)

    this.requestRedraw()
  }

  createTokenActor (t: Sim.Token) {
    let a = new TokenActor()
    a.setScale(t.radius, t.radius)
    a.setPosition(t.x, t.y)
    a.setRotation(t.rotation)
    a.setColor(t.color.r / 255, t.color.g / 255, t.color.b / 255)
    a.setIsFoe(t.isFoe)
    this.renderer.addActor(a, RenderLayers.TOKENS)
    this.tokenActors.set(t.id, a)
  }

  clearTokens () {
    this.tokens.splice(0)
    this.movingTokens.splice(0)
    this.tokenActors.forEach(actor => {
      this.renderer.removeActor(actor)
    })
    this.tokenActors.clear()
    this.requestRedraw()
  }

  onServerState (data: ServerState) {
    if (this.hasState || !data.isStateLoaded) {
      return
    }
    this.hasState = true

    // Copy the list of tokens
    this.lines.push(...data.lines)

    data.lines.forEach((t : Sim.Line) => {
      this.onNewDoodadLine(t)
    })

    data.tokens.forEach((t : Sim.Token) => {
      this.onNewToken(t)
    })

    // if (data.building !== null) {
    //   this.currentBuilding = data.building
    // } else {
    //   this.currentBuilding = undefined
    // }

    if (data.building !== null) {
      this.onServerLoadBuilding(data.building)
    }

    if (data.tilesPath.length > 0) {
      this.tileRenderer.loadFrom(data.tilesPath)
    }

    this.requestRedraw()
  }

  onServerIsGm (isGm: boolean) {
    this.doorActor.showInvisible = isGm
    this.roomActor.showInvisible = isGm
    this.wallActor.showInvisible = isGm
    this.furnitureActor.showInvisible = isGm

    this.doorActor.updateVertexData()
    this.roomActor.updateVertexData()
    this.wallActor.updateVertexData()
    this.furnitureActor.updateVertexData()
    this.requestRedraw()
  }

  onServerMoveToken (data : Sim.TokenMoveOrder) {
    // We use the same tokens as the server
    let t = this.tokenActors.get(data.token.id)
    if (t) {
      this.requestRedraw()
      data.token.displaySpeed = Math.hypot(data.token.x - data.token.displayX, data.token.y - data.token.displayY)
      let a = Math.atan2(data.token.y - data.token.displayY, data.token.x - data.token.displayX)
      t.setRotation(a)
      if (data.token.displaySpeed > 0) {
        if (this.movingTokens.indexOf(data.token) !== undefined) {
          this.movingTokens.push(data.token)
          if (this.movingTokens.length === 1) {
            // Start the updates
            this.updateMovingTokens()
          }
        }
      } else {
        t.setRotation(data.rotation)
      }
    }
  }

  onServerDeleteToken (data : Sim.Token) {
    if (this.selected !== undefined && this.selected.id === data.id) {
      this.setSelectedToken(undefined)
    }

    let pos = this.movingTokens.indexOf(data)
    if (pos !== undefined) {
      this.movingTokens.splice(pos, 1)
    }

    pos = this.tokens.findIndex((t : Sim.Token) => { return t.id === data.id })
    if (pos >= 0) {
      let a = this.tokenActors.get(data.id) as TokenActor
      if (a) {
        this.renderer.removeActor(a)
        this.tokenActors.delete(data.id)
      }
      this.tokens.splice(pos, 1)
      this.requestRedraw()
    } else {
      console.log('Asked to delete token', data,
        'but no token with that id is registered in the map.')
    }
  }

  onServerCreateLine (data: Sim.Line) {
    this.onNewDoodadLine(data)
  }

  onServerClearLines () {
    this.lines.splice(0, this.lines.length)
    this.doodadLineActors.forEach(actor => {
      this.renderer.removeActor(actor)
    })
    this.doodadLineActors.clear()
    this.requestRedraw()
  }

  onToolSelected (type: string) {
    if (type === 'view') {
      this.tool = new Tool(this)
    } else if (type === 'token') {
      this.tool = new ToolToken(this)
    } else if (type === 'line') {
      this.tool = new ToolLine(this)
    } else if (type === 'room') {
      this.tool = new ToolRoom(this)
    } else if (type === 'wall') {
      this.tool = new ToolWall(this)
    } else if (type === 'door') {
      this.tool = new ToolDoor(this)
    } else if (type === 'furniture') {
      this.tool = new ToolFurniture(this)
    } else if (type === 'reveal') {
      this.tool = new ToolReveal(this)
    }
  }

  onServerToggleFoe (token: Sim.Token) {
    let a = this.tokenActors.get(token.id) as TokenActor
    if (a) {
      a.setIsFoe(token.isFoe)
    }
    this.requestRedraw()
  }

  onServerCreateRoom (room: B.Room) {
    this.roomActor.addRoom(room)
    this.requestRedraw()
  }

  onServerDeleteRoom (r: B.Room) {
    this.roomActor.removeRoom(r)
    this.requestRedraw()
  }

  onServerModifyRoom (d: B.Room) {
    this.roomActor.updateVertexData()
    this.requestRedraw()
  }

  onServerCreateWall (w: B.Wall) {
    this.wallActor.addWall(w)
    this.requestRedraw()
  }

  onServerDeleteWall (w: B.Wall) {
    this.wallActor.removeWall(w)
    this.requestRedraw()
  }

  onServerModifyWall (d: B.Wall) {
    this.wallActor.updateVertexData()
    this.requestRedraw()
  }

  onServerCreateDoor (w: B.Door) {
    this.doorActor.addDoor(w)
    this.requestRedraw()
  }

  onServerDeleteDoor (w: B.Door) {
    this.doorActor.removeDoor(w)
    this.requestRedraw()
  }

  onServerModifyDoor (d: B.Door) {
    this.doorActor.updateVertexData()
    this.requestRedraw()
  }

  onServerCreateFurniture (w: B.Furniture) {
    this.furnitureActor.addFurniture(w)
    this.requestRedraw()
  }

  onServerDeleteFurniture (w: B.Furniture) {
    this.furnitureActor.removeFurniture(w)
    this.requestRedraw()
  }

  onServerModifyFurniture (d: B.Furniture) {
    this.furnitureActor.updateVertexData()
    this.requestRedraw()
  }

  onServerClearBuilding () {
    this.doorActor.clearDoors()
    this.furnitureActor.clearFurniture()
    this.roomActor.clearRooms()
    this.wallActor.clearWalls()
    this.requestRedraw()
  }

  onServerLoadBuilding (building: B.Building) {
    this.onServerClearBuilding()
    building.rooms.forEach((r: B.Room) => {
      this.roomActor.addRoom(r)
    })
    building.walls.forEach((w: B.Wall) => {
      this.wallActor.addWall(w)
    })
    building.doors.forEach((d: B.Door) => {
      this.doorActor.addDoor(d)
    })
    building.furniture.forEach((f: B.Furniture) => {
      this.furnitureActor.addFurniture(f)
    })
  }
}
</script>

<style scoped>
canvas {
  outline-width: 0px !important;
}
</style>
