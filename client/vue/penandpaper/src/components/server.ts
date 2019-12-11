// This handles the connection with the server, and stores any information connected to that

import Vuex, { Store, MutationPayload } from 'vuex'

import eventBus from '../eventbus'
import * as Sim from '../simulation'

// Used to communicate the simulation state of the server via the event bus
export class ServerState {
  tokens: Sim.Token[] = []
  lines: Sim.Line[] = []
}

export default class Server {
    uid: string;
    socket?: WebSocket;
    store: Store<any>;

    nextId: number = 0
    nextColor: number = 0

    tokens: Sim.Token[] = []
    lines: Sim.Line[] = []

    errorhandler?: () => void;

    constructor (store: Store<any>) {
      console.log('Creating a new server')
      this.uid = ''
      this.loadUid()
      this.store = store
      this.store.subscribe((mutation: MutationPayload, state: any) => {
        this.onmutation(mutation, state)
      })
      eventBus.$on('/chat/send', (data:string) => { this.sendChat(data) })
      eventBus.$on('/client/token/create', (data:Sim.Point) => { this.clientCreateToken(data) })
      eventBus.$on('/client/token/move', (data: Sim.TokenMoveOrder) => { this.onClientMoveToken(data) })
      eventBus.$on('/client/token/clear', () => { this.clientClearTokens() })
      eventBus.$on('/client/token/toggle_foe', (data: Sim.Token) => { this.onClientTokenToggleFoe(data) })
      eventBus.$on('/client/token/delete', (data: Sim.Token) => { this.onClientDeleteToken(data) })
      eventBus.$on('/client/line/create', (data: Sim.Line) => { this.onClientCreateLine(data) })
      eventBus.$on('/client/line/clear', () => { this.onClientClearLines() })
      eventBus.$on('/server/request_state', () => { this.onStateRequest() })
    }

    connect () {
      console.log('Connecting to the ws server.')
      let port = ':8081'
      this.socket = new WebSocket('wss://' + window.location.hostname + port)

      // Wrap the callbacks into anonymous functions to ensure they are called
      // with the correct object as 'this'
      var server = this
      this.socket.onmessage = function (msg:MessageEvent) {
        server.onmessage(msg)
      }
      this.socket.onerror = function (err:Event) {
        server.onerror(err)
      }
      this.socket.onopen = function (msg:Event) {
        server.onopen(msg)
      }
      this.socket.onclose = function (msg:Event) {
        server.onclose(msg)
      }
    }

    onmessage (msg:MessageEvent) {
      let packet = JSON.parse(msg.data)
      let type = packet['type']
      let data = packet['data']
      console.log('Received a message: ', packet)
      if (type === 'Session') {
        this.store.commit('setUsername', data['name'])
      } else if (type === 'Chat') {
        this.onchat(data)
      } else if (type === 'Init') {
        this.onServerInit(data)
      } else if (type === 'CreateToken') {
        this.onServerCreateToken(data)
      } else if (type === 'ClearTokens') {
        this.serverClearTokens()
      } else if (type === 'MoveToken') {
        this.onServerMoveToken(data)
      } else if (type === 'DeleteToken') {
        this.onServerDeleteToken(data)
      } else if (type === 'CreateDoodadLine') {
        this.onServerCreateLine(data)
      } else if (type === 'ClearDoodads') {
        this.onServerClearDoodads()
      } else if (type === 'TokenToggleFoe') {
        this.onServerTokenToggleFoe(data)
      }
    }

    onchat (data : any) {
      eventBus.$emit('/chat/message', { sender: data['sender'], message: data['message'] })
    }

    onServerInit (data: any) {
      this.nextId = data['nextId']
      this.nextColor = data['nextColor']
      for (let rawToken of data.tokens) {
        let token = new Sim.Token()
        token.x = rawToken.x
        token.y = rawToken.y
        token.id = rawToken.id
        token.isFoe = rawToken.foe
        token.color.r = rawToken.r
        token.color.g = rawToken.g
        token.color.b = rawToken.b
        // TODO: The map does not yet exist. It will have to request the entire state.
        eventBus.$emit('/server/token/create', token)
        this.tokens.push(token)
      }
      for (let rawLine of data.doodads) {
        let line = new Sim.Line()
        line.start.x = rawLine.sx
        line.start.y = rawLine.sy
        line.stop.x = rawLine.ex
        line.stop.y = rawLine.ey
        this.lines.push(line)
      }
      // TODO: handle the rest of the init packet
    }

    sendChat (text : string) {
      let packet = {
        type: 'Chat',
        uid: this.uid,
        data: {
          sender: this.store.state.username,
          message: text
        }
      }
      this.send(JSON.stringify(packet))
    }

    onServerCreateToken (data: any) {
      let token = new Sim.Token()
      token.x = data.x
      token.y = data.y
      token.radius = 0.25
      token.id = this.nextId
      token.isFoe = false
      token.color = Sim.TOKEN_COLORS[this.nextColor]

      eventBus.$emit('/server/token/create', token)
      this.tokens.push(token)

      this.nextId += 1
      this.nextColor += 1
      this.nextColor %= Sim.TOKEN_COLORS.length
    }

    clientCreateToken (pos: Sim.Point) {
      let packet = {
        type: 'CreateToken',
        uid: this.uid,
        data: {
          x: pos.x,
          y: pos.y
        }
      }
      this.send(JSON.stringify(packet))
    }

    clientClearTokens () {
      let packet = {
        type: 'ClearTokens',
        uid: this.uid,
        data: { }
      }
      this.send(JSON.stringify(packet))
    }

    serverClearTokens () {
      eventBus.$emit('/server/token/clear')
    }

    onClientMoveToken (move: Sim.TokenMoveOrder) {
      let packet = {
        type: 'MoveToken',
        uid: this.uid,
        data: {
          'x': move.x,
          'y': move.y,
          'id': move.token.id
        }
      }
      this.send(JSON.stringify(packet))
    }

    onServerMoveToken (data: any) {
      let token = this.findTokenById(data.id)
      if (token !== undefined) {
        let move = new Sim.TokenMoveOrder()
        move.x = data.x
        move.y = data.y
        move.token = token

        // Apply the move to our state
        move.token.x = move.x
        move.token.y = move.y

        eventBus.$emit('/server/token/move', move)
      } else {
        console.log('Got a move order for a token with id ', data.id, ' but was unable to find such a token.')
      }
    }

    onClientDeleteToken (data: Sim.Token) {
      let packet = {
        type: 'DeleteToken',
        uid: this.uid,
        data: {
          'id': data.id
        }
      }
      this.send(JSON.stringify(packet))
    }

    onServerDeleteToken (data: any) {
      let token = this.findTokenById(data.id)
      if (token !== undefined) {
        eventBus.$emit('/server/token/delete', token)
        let pos = this.tokens.findIndex((t: Sim.Token) => { return t.id === data.id })
        this.tokens.splice(pos, 1)
      } else {
        console.log('Received a DeleteToken packet with data', data, 'but didn\'t find a token with a matching id')
      }
    }

    onClientCreateLine (line: Sim.Line) {
      let packet = {
        type: 'CreateDoodadLine',
        uid: this.uid,
        data: {
          'sx': line.start.x,
          'sy': line.start.y,
          'ex': line.stop.x,
          'ey': line.stop.y
        }
      }
      this.send(JSON.stringify(packet))
    }

    onServerCreateLine (data: any) {
      let line = new Sim.Line()
      line.start.x = data.sx
      line.start.y = data.sy
      line.stop.x = data.ex
      line.stop.y = data.ey
      eventBus.$emit('/server/line/create', line)
      this.lines.push(line)
    }

    onClientClearLines () {
      let packet = {
        type: 'ClearDoodads',
        uid: this.uid,
        data: { }
      }
      this.send(JSON.stringify(packet))
    }

    onServerClearDoodads () {
      eventBus.$emit('/server/line/clear')
      this.lines.splice(0, this.lines.length)
    }

    onStateRequest () {
      let state = new ServerState()
      state.tokens = this.tokens
      state.lines = this.lines
      eventBus.$emit('/server/state', state)
    }

    onServerTokenToggleFoe (data: any) {
      let token = this.findTokenById(data.id)
      if (token !== undefined) {
        token.isFoe = !token.isFoe
        eventBus.$emit('/server/token/toggle_foe', token)
      } else {
        console.log('The server requested an is foe change, but no token with the given id exists: ', data)
      }
    }

    onClientTokenToggleFoe (token: Sim.Token) {
      let packet = {
        type: 'TokenToggleFoe',
        uid: this.uid,
        data: {
          'id': token.id
        }
      }
      this.send(JSON.stringify(packet))
    }

    onerror (err:Event) {
      err.stopPropagation()
      console.log('Unable to connect to the server')
      console.log('errorhandler ' + this.errorhandler)
      if (this.errorhandler) {
        this.errorhandler()
      }
    }

    onopen (ev:Event) {
      console.log('Connected to the server')
      // Send the session init
      let packet = {
        type: 'InitSession',
        data: { uid: this.uid }
      }
      this.send(JSON.stringify(packet))
    }

    onclose (ev:Event) {
      console.log('The connection to the server was closed')
      if (this.errorhandler) {
        this.errorhandler()
      }
    }

    setErrorHandler (handler: () => void) {
      this.errorhandler = handler
      console.log('The errorhandler is ' + this.errorhandler)
    }

    send (data : string) {
      if (this.socket) {
        this.socket.send(data)
      }
    }

    loadUid () {
      if (localStorage.uid) {
        this.uid = localStorage.uid
      }
      this.uid = ''
      for (var i = 0; i < 32; i++) {
        // pick a char from [33;126]
        let v = 33 + Math.round(Math.random() * 93.5)
        this.uid += String.fromCharCode(v)
      }
      localStorage.uid = this.uid
    }

    onmutation (mutation: MutationPayload, state: any) {
      if (mutation.type === 'setUsername') {
        if (state.username.length > 0) {
          let packet = {
            type: 'SetUsername',
            uid: this.uid,
            data: { name: state.username }
          }
          this.send(JSON.stringify(packet))
        }
      }
    }

    findTokenById (id: number) : Sim.Token | undefined {
      return this.tokens.find((token: Sim.Token) => { return token.id === id })
    }
}
