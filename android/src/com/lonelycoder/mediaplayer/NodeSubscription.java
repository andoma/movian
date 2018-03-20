package com.lonelycoder.mediaplayer;

import java.io.File;
import android.content.Context;
import android.util.Log;

public class NodeSubscription implements NodeSubscriptionCallback {

    private int id;
    private Callback mCb;
    private NodeFactory mFactory;

    public NodeSubscription(Prop p, String path, NodeFactory f, Callback cb) {
        mFactory = f;
        mCb = cb;
        id = Core.subNodes((int)(p != null ? p.getPropId() : 0), path, this);
    }

    public void stop() {
        if(id != 0) {
            Core.unSub(id);
            id = 0;
        }
    }

    protected void finalize() {
        if(id != 0)
            Core.unSub(id);
    }

    public void addNodes(int[] nodes, int before) {
        Prop vec[] = new Prop[nodes.length];
        for (int i = 0; i < nodes.length; i++) {
            vec[i] = mFactory.makeNode(nodes[i]);
        }
        mCb.addNodes(vec, before);
    }


    public void delNodes(int[] nodes) {
        mCb.delNodes(nodes);
    }


    public void moveNode(int node, int before) {
        mCb.moveNode(node, before);
    }


    interface Callback {
        public void addNodes(Prop vec[], int before);
        public void delNodes(int nodes[]);
        public void moveNode(int node, int before);


    }
}
