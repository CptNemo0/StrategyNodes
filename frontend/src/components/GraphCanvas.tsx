import {
  addEdge,
  Background,
  Controls,
  MiniMap,
  ReactFlow,
  useEdgesState,
  useNodesState,
  type Connection,
  type Edge,
  type Node,
} from "@xyflow/react";
import { useCallback } from "react";

import "@xyflow/react/dist/style.css";

// Placeholder graph until node kinds (feeds, indicators, logic) get their own
// custom node components backed by the C++ evaluation engine.
const kInitialNodes: Node[] = [
  {
    id: "kraken-feed",
    position: { x: 40, y: 120 },
    data: { label: "Kraken Feed — BTC/USD" },
    type: "input",
  },
  {
    id: "microprice",
    position: { x: 320, y: 120 },
    data: { label: "Microprice" },
  },
];

const kInitialEdges: Edge[] = [
  { id: "kraken-feed->microprice", source: "kraken-feed", target: "microprice" },
];

export default function GraphCanvas() {
  const [nodes, , onNodesChange] = useNodesState(kInitialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(kInitialEdges);

  const onConnect = useCallback(
    (connection: Connection) =>
      setEdges((current) => addEdge(connection, current)),
    [setEdges],
  );

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      onNodesChange={onNodesChange}
      onEdgesChange={onEdgesChange}
      onConnect={onConnect}
      colorMode="dark"
      fitView
    >
      <Background />
      <Controls />
      <MiniMap />
    </ReactFlow>
  );
}
