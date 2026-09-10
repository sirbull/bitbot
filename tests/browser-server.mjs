import { createServer } from '../tools/dev-server.mjs';
import { Simulator } from '../lib/simulator.mjs';
createServer(new Simulator({ delay: 100 })).listen(4174, '127.0.0.1');
