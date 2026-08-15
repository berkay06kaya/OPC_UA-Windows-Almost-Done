#include "source/OpcUaBrowser.h"
#include "core/Logger.h"

#include <iostream>
#include <open62541/client.h>
#include <open62541/client_highlevel.h>

std::vector<BrowseNode> OpcUaBrowser::browse(UA_Client* client, const std::string& nodeId) {
    std::vector<BrowseNode> out;
    if (client == nullptr) return out;

    UA_NodeId startId;
    if (nodeId.empty()) {
        startId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    } else {
        UA_NodeId_init(&startId);
        if (UA_NodeId_parse(&startId, UA_STRING_ALLOC(nodeId.c_str())) != UA_STATUSCODE_GOOD) {
            LOG_ERROR() << "[BROWSE] gecersiz nodeId: " << nodeId;
            return out;
        }
    }

    UA_BrowseRequest req;
    UA_BrowseRequest_init(&req);
    req.requestedMaxReferencesPerNode = 0;
    req.nodesToBrowseSize = 1;
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = startId;
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE | UA_NODECLASS_METHOD;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    req.nodesToBrowse = &bd;

    auto appendRefs = [&out](const UA_BrowseResult& res) {
        for (size_t i = 0; i < res.referencesSize; i++) {
            const UA_ReferenceDescription& r = res.references[i];
            BrowseNode n;
            UA_String idStr = UA_STRING_NULL;
            UA_NodeId_print(&r.nodeId.nodeId, &idStr);
            if (idStr.data) n.nodeId.assign(reinterpret_cast<char*>(idStr.data), idStr.length);
            UA_String_clear(&idStr);

            if (r.browseName.name.data)
                n.browseName = std::to_string(r.browseName.namespaceIndex) + ":" +
                    std::string(reinterpret_cast<char*>(r.browseName.name.data), r.browseName.name.length);
            if (r.displayName.text.data)
                n.displayName.assign(reinterpret_cast<char*>(r.displayName.text.data), r.displayName.text.length);

            switch (r.nodeClass) {
                case UA_NODECLASS_OBJECT:   n.nodeClass = BrowseNode::Class::Object;   n.expandable = true;  break;
                case UA_NODECLASS_VARIABLE: n.nodeClass = BrowseNode::Class::Variable; n.expandable = false; break;
                case UA_NODECLASS_METHOD:   n.nodeClass = BrowseNode::Class::Method;   n.expandable = false; break;
                default:                    n.nodeClass = BrowseNode::Class::Other;    n.expandable = false; break;
            }
            out.push_back(std::move(n));
        }
    };

    UA_BrowseResponse resp = UA_Client_Service_browse(client, req);
    if (resp.resultsSize == 1 && resp.results[0].statusCode == UA_STATUSCODE_GOOD) {
        appendRefs(resp.results[0]);

        UA_ByteString cp;
        UA_ByteString_init(&cp);
        UA_ByteString_copy(&resp.results[0].continuationPoint, &cp);
        while (cp.length > 0) {
            UA_BrowseNextRequest nreq;
            UA_BrowseNextRequest_init(&nreq);
            nreq.continuationPointsSize = 1;
            nreq.continuationPoints = &cp;
            nreq.releaseContinuationPoints = false;

            UA_BrowseNextResponse nresp = UA_Client_Service_browseNext(client, nreq);
            UA_ByteString_clear(&cp);
            if (nresp.resultsSize == 1 && nresp.results[0].statusCode == UA_STATUSCODE_GOOD) {
                appendRefs(nresp.results[0]);
                UA_ByteString_copy(&nresp.results[0].continuationPoint, &cp);
            }
            UA_BrowseNextResponse_clear(&nresp);
        }
    } else {
        LOG_ERROR() << "[BROWSE] browse basarisiz (parent=" << (nodeId.empty() ? "Objects" : nodeId) << ")";
    }

    UA_BrowseResponse_clear(&resp);
    if (!nodeId.empty()) UA_NodeId_clear(&startId);

    std::cout << "[BROWSE] parent=" << (nodeId.empty() ? "Objects" : nodeId)
              << " cocuk=" << out.size() << std::endl;
    return out;
}
