//
// Copyright (C) 2010 Alfonso Ariza, Malaga University
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#ifndef __DIJKSTRA_K_SHORTEST__H__
#define __DIJKSTRA_K_SHORTEST__H__

#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <limits>
#include "inet/networklayer/common/L3Address.h"

namespace inet{

typedef L3Address NodeId;

enum Metricts {aditiveMin,concaveMin,aditiveMax,concaveMax};
enum StateLabel {perm,tent};

const L3Address UndefinedAddr;

class DijkstraKshortest
{
protected:

	class Cost
	{
	public:
	    double value;
	    Metricts metric;
	    Cost& operator=(const Cost& cost)
	    {
	        value=cost.value;
	        metric=cost.metric;
	        return *this;
	    }
	};
	typedef std::vector<Cost> CostVector;
    void addCost(CostVector &,const CostVector & a, const CostVector & b);
    static CostVector minimumCost;
    static CostVector maximumCost;
    friend bool operator < ( const DijkstraKshortest::CostVector& x, const DijkstraKshortest::CostVector& y );

public:
    typedef std::vector<NodeId> Route;
    typedef std::vector<Route> Kroutes;
protected:
    typedef std::map<NodeId,Kroutes> MapRoutes;
    MapRoutes kRoutesMap;


    class SetElem
    {
    public:
        L3Address iD;
        int idx;
        DijkstraKshortest::CostVector cost;
        SetElem()
        {
            iD = UndefinedAddr;
            idx = -1;
        }
        SetElem& operator=(const SetElem& val)
        {
            this->iD=val.iD;
            this->idx=val.idx;
            this->cost.clear();
            for (unsigned int i=0;i<val.cost.size();i++)
                this->cost.push_back(val.cost[i]);
            return *this;
        }
    };
    friend bool operator < ( const DijkstraKshortest::SetElem& x, const DijkstraKshortest::SetElem& y );
    class State
    {
    public:
        CostVector cost;
        NodeId idPrev;
        int idPrevIdx;
        StateLabel label;
        State();
        State(const CostVector &cost);
        ~State();
        void setCostVector(const CostVector &cost);
    };

    struct Edge
    {
        NodeId last_node_; // last node to reach node X
        CostVector cost;
        Edge() {cost = maximumCost;}
        virtual ~Edge() {cost.clear();}
        inline NodeId& last_node() {return last_node_;}
        virtual double&   Cost()     {return cost[0].value;}
        virtual double&   Delay()     {return cost[1].value;}
        virtual double&   Bandwith()     {return cost[2].value;}
        virtual double&   Quality()     { return cost[3].value;}
    };

    struct EdgeWs : public Edge
    {
        virtual double&   Cost()     {return cost[0].value;}
        virtual double&   Bandwith()     {return cost[1].value;}
        virtual double&   Delay()     { return cost[3].value;}
        virtual double&   Quality()     { return cost[3].value;}
    };

    struct EdgeSw : public Edge
    {
        virtual double&   Cost()     {return cost[1].value;}
        virtual double&   Bandwith()     {return cost[0].value;}
        virtual double&   Delay()     { return cost[3].value;}
        virtual double&   Quality()     { return cost[3].value;}
    };

    typedef std::vector<DijkstraKshortest::State> StateVector;
    typedef std::map<NodeId,DijkstraKshortest::StateVector> RouteMap;
    typedef std::map<NodeId, std::vector<DijkstraKshortest::Edge*> > LinkArray;
    LinkArray linkArray;
    RouteMap routeMap;
    NodeId rootNode;
    int K_LIMITE;
    CostVector limitsData;
public:
    DijkstraKshortest(int);
    virtual ~DijkstraKshortest();
    virtual bool isRouteMapEmpty() {return routeMap.empty();}
    virtual void clearRouteMap() {routeMap.clear();}
    virtual void setFromTopo(const cTopology *,L3Address::AddressType = L3Address::IPv4);
    virtual void setLimits(const std::vector<double> &);
    virtual void resetLimits(){limitsData.clear();}
    virtual void setKLimit(int val){if (val>0) K_LIMITE=val;}
    virtual void initMinAndMax();
    virtual void initMinAndMaxWs();
    virtual void initMinAndMaxSw();
    virtual void cleanLinkArray();

    virtual void deleteEdge(const NodeId &, const NodeId &);
    virtual Edge* removeEdge(const NodeId & originNode, const NodeId & last_node);
    virtual const Edge* getEdge(const NodeId & originNode, const NodeId & last_node);

    virtual void addEdge (const NodeId & dest_node, const NodeId & last_node,double cost) {
        routeMap.clear();
        addEdge (dest_node, last_node,cost,1,1000,1000);
    }

    virtual void addEdge (const NodeId & dest_node, const NodeId & last_node,double cost,double delay,double bw,double quality);
    virtual void addEdgeWs (const NodeId & dest_node, const NodeId & last_node, double costAdd, double concave);
    virtual void addEdgeSw (const NodeId & dest_node, const NodeId & last_node, double costAdd, double concave);
    virtual void setRoot(const NodeId & dest_node);
    virtual bool nodeExist(const NodeId &);

    virtual void run();
    virtual void runUntil (const NodeId &);
    virtual int getNumRoutes(const NodeId &nodeId);
    virtual bool getRoute(const NodeId &nodeId,std::vector<NodeId> &pathNode,int k=0);
    virtual void getAllRoutes(std::map<NodeId, std::vector<std::vector<NodeId> > > & routes);

    virtual void setRouteMapK();
    virtual void getRouteMapK(const NodeId &nodeId, Kroutes &routes);
};

typedef std::vector<std::pair<NodeId, NodeId> > NodePairs;
class Dijkstra
{
protected:
    enum StateLabel
    {
        perm, tent
    };

public:
    enum Method
    {
        basic, widestshortest, shortestwidest,otherCost
    };

protected :
    Method method;

public:

    typedef std::vector<NodeId> Route;
    typedef std::map<NodeId, Route> MapRoutes;
    struct RoutesInfo {
        NodeId destination;
        NodeId nextHop;
        double cost = 0;
        int hops = -1;
    };

    typedef std::vector<RoutesInfo> RoutesInfoList;


    class SetElem
    {
    public:
        NodeId iD;
        Method m;
        double cost;
        double costConcave = std::numeric_limits<double>::quiet_NaN();
        double costAdd2 = std::numeric_limits<double>::quiet_NaN();
        double costConcave2 = std::numeric_limits<double>::quiet_NaN();
        SetElem()
        {
            iD = UndefinedAddr;
            cost = std::numeric_limits<double>::max();
            costConcave = 0;
            costAdd2 = std::numeric_limits<double>::max();
            costConcave2 = 0;

            m = basic;
        }
        SetElem& operator=(const SetElem& val)
        {
            this->iD = val.iD;
            this->costConcave = val.costConcave;
            this->costConcave2 = val.costConcave2;
            this->costAdd2 = val.costAdd2;
            this->cost = val.cost;
            return *this;
        }
    };

    friend bool operator <(const Dijkstra::SetElem& x, const Dijkstra::SetElem& y);
    friend bool operator >(const Dijkstra::SetElem& x, const Dijkstra::SetElem& y);
    class State
    {
    public:
        double cost = std::numeric_limits<double>::max();
        double costConcave = 0;
        double costAdd2 = std::numeric_limits<double>::max();
        double costConcave2 = 0;
        NodeId idPrev;
        StateLabel label;
        State();
        State(const double &cost, const double &);
        State(const double &cost, const double &, const double &, const double &);
        virtual ~State();
    };

    struct Edge
    {
        NodeId last_node_; // last node to reach node X
        double cost;
        double costConcave;
        double costAdd2 = std::numeric_limits<double>::max();
        double costConcave2 = 0;
        Edge()
        {
            cost = std::numeric_limits<double>::max();
            costConcave = 0;
            last_node_ = UndefinedAddr;
        }

        Edge(const Edge &other)
        {
            last_node_ = other.last_node_;
            cost = other.cost;
            costConcave = other.costConcave;
        }

        virtual Edge *dup() const {return new Edge(*this);}

        virtual ~Edge()
        {

        }

        inline NodeId& last_node()
        {
            return last_node_;
        }

        virtual double& Cost()
        {
            return cost;
        }

        virtual double& Cost2()
        {
            return costConcave;
        }

    };

    typedef std::map<NodeId, Dijkstra::State> RouteMap;
    typedef std::map<NodeId, std::vector<Dijkstra::Edge*> > LinkArray;
protected:
    LinkArray linkArray;
    RouteMap routeMap;
    NodeId rootNode;
public:

    Dijkstra();
    virtual ~Dijkstra();

    virtual bool isRouteMapEmpty() {return routeMap.empty();}
    virtual void clearRouteMap() {routeMap.clear();}
    virtual void discoverPartitionedLinks(std::vector<NodeId> &pathNode, const LinkArray &, NodePairs &);
    virtual void discoverAllPartitionedLinks(const LinkArray & topo, NodePairs &links);

    virtual void discoverPartitionedLinks(std::vector<NodeId> &pathNode, NodePairs &array) {
        discoverPartitionedLinks(pathNode, linkArray, array);
    }

    virtual void discoverAllPartitionedLinks(NodePairs &links) {
        discoverAllPartitionedLinks(linkArray, links);
    }

    virtual void setFromTopo(const cTopology *, L3Address::AddressType type = L3Address::IPv4);
/*    virtual void setFromDijkstraFuzzy(const DijkstraFuzzy::LinkArray &);
    virtual void setFromDijkstraFuzzy(const DijkstraFuzzy::LinkArray &, LinkArray &);*/

    virtual void cleanLinkArray(LinkArray &);
    virtual void addEdge(const NodeId & dest_node, const NodeId & last_node, const double &cost, const double &cost2, LinkArray &);
    virtual void addEdge(const NodeId & dest_node, Edge*, LinkArray &);
    virtual void deleteEdge(const NodeId &, const NodeId &, LinkArray &);
    virtual Edge* removeEdge(const NodeId & originNode, const NodeId & last_node, LinkArray & linkArray);
    virtual const Edge* getEdge(const NodeId & originNode, const NodeId & last_node, LinkArray & linkArray);

    bool rootNodeIncluded() {
        auto it = linkArray.find(rootNode);
          if (it == linkArray.end())
              return false;
          return true;
    }
    virtual void cleanLinkArray();
    virtual void clearAll();
    virtual void cleanRoutes() {routeMap.clear();}
    virtual void addEdge(const NodeId & dest_node, const NodeId & last_node, const double &cost, const double &cost2);
    virtual void addEdge(const NodeId & dest_node, Edge *);
    virtual void deleteEdge(const NodeId &, const NodeId &);
    virtual const Edge* getEdge(const NodeId & originNode, const NodeId & last_node);


    virtual bool nodeExist(const NodeId &);

    virtual void setRoot(const NodeId & dest_node);
    virtual void run(const NodeId &, const LinkArray &, RouteMap &);
    virtual void runUntil(const NodeId &, const NodeId &, const LinkArray &, RouteMap &);
    virtual void run();
    virtual void runUntil(const NodeId &);
    virtual bool getRoute(const NodeId &nodeId, std::vector<NodeId> &pathNode);
    virtual bool getRoute(const NodeId &, std::vector<NodeId> &, const RouteMap &);

    virtual void getRoutes(std::map<NodeId, std::vector<NodeId>> &);
    virtual void getRoutes(std::map<NodeId, std::vector<NodeId>> &, const RouteMap &);
    virtual unsigned int getNumRoutes() {return routeMap.size();}

    virtual double getCost(const NodeId &, const RouteMap &);
    virtual double getCost(const NodeId &);

    virtual void setMethod(Method p) {method = p;}

    virtual void getRoutesInfoList(RoutesInfoList & list);
};

}

#endif
