
#include "controllers/GraphController.h"
#include "session/PluginManager.h"

namespace Element {

GraphController::GraphController (GraphProcessor& pg, PluginManager& pm)
    : pluginManager (pm), processor (pg), lastUID (0)
{ }

GraphController::~GraphController() { }

uint32 GraphController::getNextUID() noexcept
{
    return ++lastUID;
}

int GraphController::getNumFilters() const noexcept
{
    return processor.getNumNodes();
}

const GraphNodePtr GraphController::getNode (const int index) const noexcept
{
    return processor.getNode (index);
}

const GraphNodePtr GraphController::getNodeForId (const uint32 uid) const noexcept
{
    return processor.getNodeForId (uid);
}

GraphNode* GraphController::createFilter (const PluginDescription* desc, double x, double y, uint32 nodeId)
{
    String errorMessage;
    auto* instance = pluginManager.createAudioPlugin (*desc, errorMessage);
    GraphNode* node = nullptr;
    if (instance != nullptr)
        node = processor.addNode (instance, nodeId);
    return node;
}
    
uint32 GraphController::addFilter (const PluginDescription* desc, double x, double y, uint32 nodeId)
{
    if (! desc)
    {
        AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                     TRANS ("Couldn't create filter"),
                                     "Cannot instantiate plugin without a description");
        return KV_INVALID_NODE;
    }

    if (auto* node = createFilter (desc, x, y, nodeId))
    {
        nodeId = node->nodeId;
        node->properties.set ("x", x);
        node->properties.set ("y", y);
        ValueTree model = node->getMetadata().createCopy();
        model.setProperty (Tags::object, node, nullptr);
        nodes.addChild (model, -1, nullptr);
        changed();
    }
    else
    {
        nodeId = KV_INVALID_NODE;
        AlertWindow::showMessageBox (AlertWindow::WarningIcon, "Couldn't create filter",
                                     "The plugin could not be instantiated");
    }

    return nodeId;
}

void GraphController::removeFilter (const uint32 uid)
{
    if (! processor.removeNode (uid))
        return;
    
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const Node node (nodes.getChild(i), false);
        if (node.getNodeId() == uid)
        {
            nodes.removeChild (node.getValueTree(), nullptr);
        }
    }
    
    jassert(nodes.getNumChildren() == getNumFilters());
    processorArcsChanged();
}

void GraphController::disconnectFilter (const uint32 id)
{
    if (processor.disconnectNode (id))
        processorArcsChanged();
}

void GraphController::removeIllegalConnections()
{
    if (processor.removeIllegalConnections())
        processorArcsChanged();
}

int GraphController::getNumConnections() const noexcept
{
    jassert(arcs.getNumChildren() == processor.getNumConnections());
    return arcs.getNumChildren();
}

const GraphProcessor::Connection* GraphController::getConnection (const int index) const noexcept
{
    return processor.getConnection (index);
}

const GraphProcessor::Connection* GraphController::getConnectionBetween (uint32 sourceFilterUID, int sourceFilterChannel,
                                                                          uint32 destFilterUID, int destFilterChannel) const noexcept
{
    return processor.getConnectionBetween (sourceFilterUID, sourceFilterChannel,
                                           destFilterUID, destFilterChannel);
}

bool GraphController::canConnect (uint32 sourceFilterUID, int sourceFilterChannel,
                                  uint32 destFilterUID, int destFilterChannel) const noexcept
{
    return processor.canConnect (sourceFilterUID, sourceFilterChannel,
                                 destFilterUID, destFilterChannel);
}

bool GraphController::addConnection (uint32 sourceFilterUID, int sourceFilterChannel,
                                     uint32 destFilterUID, int destFilterChannel)
{
    const bool result = processor.addConnection (sourceFilterUID, (uint32)sourceFilterChannel,
                                                 destFilterUID, (uint32)destFilterChannel);
    if (result)
        processorArcsChanged();

    return result;
}

void GraphController::removeConnection (const int index)
{
    processor.removeConnection (index);
    processorArcsChanged();
}

void GraphController::removeConnection (uint32 sourceNode, uint32 sourcePort,
                                        uint32 destNode, uint32 destPort)
{
    if (processor.removeConnection (sourceNode, sourcePort, destNode, destPort))
        processorArcsChanged();
}

void GraphController::setNodeModel (const Node& node)
{
    clear();
    graph   = node.getValueTree();
    arcs    = node.getArcsValueTree();
    nodes   = node.getNodesValueTree();
    Array<ValueTree> failed;
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        Node node (nodes.getChild(i), false);
        PluginDescription desc; node.getPluginDescription (desc);
        if (GraphNodePtr obj = createFilter (&desc, 0.0, 0.0, node.getNodeId()))
        {
            MemoryBlock state;
            if (state.fromBase64Encoding (node.node().getProperty(Tags::state).toString()))
                obj->getAudioProcessor()->setStateInformation (state.getData(), (int)state.getSize());
            node.getValueTree().setProperty(Tags::object, obj.get(), nullptr);
        }
        else
        {
            DBG("[EL] couldn't create node: " << node.getName());
            failed.add (node.getValueTree());
        }
    }
    for (const auto& n : failed)
        nodes.removeChild (n, nullptr);
    
    jassert (nodes.getNumChildren() == getNumFilters());
    
    for (int i = 0; i < arcs.getNumChildren(); ++i)
    {
        const ValueTree arc (arcs.getChild (i));
        processor.addConnection ((uint32)(int) arc.getProperty (Tags::sourceNode),
                                 (uint32)(int) arc.getProperty (Tags::sourcePort),
                                 (uint32)(int) arc.getProperty (Tags::destNode),
                                 (uint32)(int) arc.getProperty (Tags::destPort));
    }
    
    jassert (arcs.getNumChildren() == getNumConnections());
    processorArcsChanged();
}

void GraphController::savePluginStates()
{
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        ValueTree tree = nodes.getChild (i);
        const Node node (tree, false);
        MemoryBlock state;
        if (GraphNodePtr obj = node.getGraphNode())
            if (auto* proc = obj->getAudioProcessor())
                proc->getStateInformation (state);
        if (state.getSize() > 0)
            tree.setProperty (Tags::state, state.toBase64Encoding(), nullptr);
    }
}

void GraphController::clear()
{
    processor.clear();
    if (graph.isValid())
    {
        graph.removeChild (arcs, nullptr);
        graph.removeChild (nodes, nullptr);
        nodes.removeAllChildren (nullptr);
        arcs.removeAllChildren (nullptr);
        graph.addChild (nodes, -1, nullptr);
        graph.addChild (arcs, -1, nullptr);
    }
    changed();
}

void GraphController::processorArcsChanged()
{
    ValueTree newArcs = ValueTree (Tags::arcs);
    for (int i = 0; i < processor.getNumConnections(); ++i)
        newArcs.addChild (Node::makeArc (*processor.getConnection(i)), -1, nullptr);
    const auto index = graph.indexOf (arcs);
    graph.removeChild (arcs, nullptr);
    graph.addChild (newArcs, index, nullptr);
    arcs = graph.getChildWithName (Tags::arcs);
    changed();
}

}
