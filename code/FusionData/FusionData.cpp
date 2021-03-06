/*
    Ray -- Parallel genome assemblies for parallel DNA sequencing
    Copyright (C) 2010, 2011, 2012, 2013 Sébastien Boisvert

	http://DeNovoAssembler.SourceForge.Net/

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You have received a copy of the GNU General Public License
    along with this program (gpl-3.0.txt).  
	see <http://www.gnu.org/licenses/>

*/

#include "FusionData.h"

#include <RayPlatform/core/OperatingSystem.h>
#include <RayPlatform/communication/Message.h>

#include <sstream>
#include <assert.h>

__CreatePlugin(FusionData);

 /**/
 /**/
__CreateSlaveModeAdapter(FusionData,RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS); /**/
 /**/
 /**/

using namespace std;

#define SHOW_FUSION

void FusionData::processCheckpoints(){

	/** read the checkpoint ContigPaths */
	if(!m_processedCheckpoint){
		m_processedCheckpoint=true;

		if(m_parameters->hasCheckpoint("ContigPaths")){
			cout<<"Rank "<<m_parameters->getRank()<<" is reading checkpoint ContigPaths"<<endl;
			ifstream f(m_parameters->getCheckpointFile("ContigPaths").c_str());
	
			/* delete old stuff */
			m_ed->m_EXTENSION_identifiers.clear();
			m_ed->m_EXTENSION_contigs.clear();
	
			int theSize=0;
			f.read((char*)&theSize,sizeof(int));
	
			/* write each path with its name and vertices */
			for(int i=0;i<theSize;i++){
				PathHandle name=0;
				int vertices=0;
				f.read((char*)&name,sizeof(PathHandle));
				f.read((char*)&vertices,sizeof(int));
				GraphPath path;
				path.setKmerLength(m_parameters->getWordSize());
				for(int j=0;j<vertices;j++){
					Kmer kmer;
					kmer.read(&f);
					path.push_back(&kmer);
				}
	
				#ifdef CONFIG_ASSERT
				assert(vertices!=0);
				assert(vertices == (int)path.size());
				#endif
	
				m_ed->m_EXTENSION_identifiers.push_back(name);
				m_ed->m_EXTENSION_contigs.push_back(path);
			}
			f.close();
		}
	}
}

void FusionData::call_RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS(){

	processCheckpoints();

	if(!isReady()){
		return;
	}

	if(!m_buffers.isEmpty() && m_seedingData->m_SEEDING_i==(LargeCount)m_ed->m_EXTENSION_contigs.size()){
		m_ready+=m_buffers.flushAll(RAY_MPI_TAG_SAVE_WAVE_PROGRESSION_WITH_REPLY,m_outboxAllocator,m_outbox,getRank());
		return;

	}else if(m_buffers.isEmpty() && m_seedingData->m_SEEDING_i==(LargeCount)m_ed->m_EXTENSION_contigs.size()){
		printf("Rank %i is distributing fusions [%i/%i] (completed)\n",getRank(),(int)m_ed->m_EXTENSION_contigs.size(),(int)m_ed->m_EXTENSION_contigs.size());

		Message aMessage(NULL,0,MASTER_RANK,RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED,getRank());
		m_outbox->push_back(&aMessage);
		(*m_mode)=RAY_SLAVE_MODE_DO_NOTHING;
		m_buffers.showStatistics(m_parameters->getRank());
		m_buffers.clear();

		m_cacheForRepeatedVertices.clear();
		m_cacheAllocator.clear();

		if(m_parameters->showMemoryUsage()){
			showMemoryUsage(m_rank);
			showDate();
		}

		return;
	}

	if(m_ed->m_EXTENSION_currentPosition==0){
		if(m_seedingData->m_SEEDING_i%10==0){
			printf("Rank %i is distributing fusions [%i/%i]\n",getRank(),(int)(m_seedingData->m_SEEDING_i+1),(int)m_ed->m_EXTENSION_contigs.size());

			if(m_parameters->showMemoryUsage()){
				showMemoryUsage(getRank());
				showDate();
			}
		}
	}

	#ifdef CONFIG_ASSERT
	assert(m_seedingData->m_SEEDING_i<m_ed->m_EXTENSION_contigs.size());
	assert(m_ed->m_EXTENSION_currentPosition < (int) m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size());
	assert(m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size() > 0);
	#endif

	Kmer vertex;
	m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].at(m_ed->m_EXTENSION_currentPosition,&vertex);

	Rank destination=m_parameters->vertexRank(&vertex);

	for(int i=0;i<KMER_U64_ARRAY_SIZE;i++){
		m_buffers.addAt(destination,vertex.getU64(i));
	}

	m_buffers.addAt(destination,m_ed->m_EXTENSION_identifiers[m_seedingData->m_SEEDING_i].getValue());
	m_buffers.addAt(destination,m_ed->m_EXTENSION_currentPosition);

	if(m_buffers.flush(destination,KMER_U64_ARRAY_SIZE+2,RAY_MPI_TAG_SAVE_WAVE_PROGRESSION_WITH_REPLY,m_outboxAllocator,m_outbox,getRank(),false)){
		m_ready++;
	}

	m_ed->m_EXTENSION_currentPosition++;

	// the next one
	if(m_ed->m_EXTENSION_currentPosition==(int)m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()){
		m_seedingData->m_SEEDING_i++;
		m_ed->m_EXTENSION_currentPosition=0;
	}
}

void FusionData::readyBuffers(){
	m_buffers.constructor(m_size,MAXIMUM_MESSAGE_SIZE_IN_BYTES/sizeof(MessageUnit),
		"RAY_MALLOC_TYPE_FUSION_BUFFERS",m_parameters->showMemoryAllocations(),KMER_U64_ARRAY_SIZE+2);
}

void FusionData::constructor(int size,int max,int rank,StaticVector*outbox,
		RingAllocator*outboxAllocator,int wordSize,
		ExtensionData*ed,SeedingData*seedingData,int*mode,Parameters*parameters){
	m_parameters=parameters;
	m_processedCheckpoint=false;
	m_debugFusionCode=m_parameters->hasOption("-debug-fusions");
	m_seedingData=seedingData;
	m_cacheAllocator.constructor(4194304,"RAY_MALLOC_TYPE_FUSION_CACHING",m_parameters->showMemoryAllocations());
	m_cacheForRepeatedVertices.constructor();
	m_mode=mode;
	m_ed=ed;
	m_size=size;
	m_rank=rank;
	m_outbox=outbox;
	m_outboxAllocator=outboxAllocator;
	m_wordSize=wordSize;
	#ifdef CONFIG_ASSERT
	assert(m_wordSize>0);
	#endif
	
	m_FINISH_pathsForPosition=new vector<vector<Direction> >;
	m_mappingConfirmed=false;
	m_validationPosition=0;
	m_Machine_getPaths_INITIALIZED=false;
	m_Machine_getPaths_DONE=false;
}

int FusionData::getRank(){
	return m_rank;
}

int FusionData::getSize(){
	return m_size;
}

FusionData::FusionData(){
	m_ready=0;
}

void FusionData::setReadiness(){
	m_ready--;
}

bool FusionData::isReady(){
	return m_ready==0;
}

/*
 * find overlap between extensions
 *
 * example:
 *
 *
 *
 *         ----------------------->
 *                          ----------------------->
 */
void FusionData::finishFusions(){
	if(m_seedingData->m_SEEDING_i==(LargeCount)m_ed->m_EXTENSION_contigs.size()){
		printf("Rank %i is finishing fusions [%i/%i] (completed)\n",getRank(),(int)m_ed->m_EXTENSION_contigs.size(),(int)m_ed->m_EXTENSION_contigs.size());

		if(m_parameters->showMemoryUsage()){
			showMemoryUsage(m_rank);
			showDate();
		}

		MessageUnit*message=(MessageUnit*)m_outboxAllocator->allocate(1*sizeof(MessageUnit));
		message[0]=m_FINISH_fusionOccured;
		Message aMessage(message,1,MASTER_RANK,RAY_MPI_TAG_FINISH_FUSIONS_FINISHED,getRank());
		m_outbox->push_back(&aMessage);
		(*m_mode)=RAY_SLAVE_MODE_DO_NOTHING;

		m_cacheForRepeatedVertices.clear();
		m_cacheAllocator.clear();

		#ifdef CONFIG_ASSERT
		assert(m_FINISH_pathsForPosition!=NULL);
		#endif
		delete m_FINISH_pathsForPosition;
		m_FINISH_pathsForPosition=new vector<vector<Direction> >;
		return;
	}
	int overlapMinimumLength=2000;

	/** require a larger minimum overlap for larger contigs, that make sense */
	double ratio=0.3;
	int candidateOverlapLength=(int)(m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()*ratio);
	if(candidateOverlapLength>overlapMinimumLength)
		overlapMinimumLength=candidateOverlapLength;

	if((int)m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()<overlapMinimumLength){
	
		if(m_seedingData->m_SEEDING_i%10==0){
			printf("Rank %i is finishing fusions [%i/%i]\n",getRank(),(int)m_seedingData->m_SEEDING_i+1,(int)m_ed->m_EXTENSION_contigs.size());
	
			if(m_parameters->showMemoryUsage()){
				showMemoryUsage(getRank());
				showDate();
			}
		}

		m_seedingData->m_SEEDING_i++;
		m_FINISH_vertex_requested=false;
		m_ed->m_EXTENSION_currentPosition=0;
		m_FUSION_pathLengthRequested=false;
		m_Machine_getPaths_INITIALIZED=false;
		m_Machine_getPaths_DONE=false;
		m_checkedValidity=false;

		return;
	}
	// check if the path begins with someone else.
	
	PathHandle currentId=m_ed->m_EXTENSION_identifiers[m_seedingData->m_SEEDING_i];

	#ifdef CONFIG_ASSERT
	assert(getRankFromPathUniqueId(currentId)<m_size);
	#endif

	// don't do it if it is removed.

	// start threading the extension
	// as the algorithm advance on it, it stores the path positions.
	// when it reaches a choice, it will use the available path as basis.
	
	// we have the extension in m_ed->m_EXTENSION_contigs[m_SEEDING_i]
	// we get the paths with getPaths
	bool done=false;

	int capLength=80;

	if(m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()>20000)
		capLength=256;

	int position1=m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()-1-capLength;
	int position2=m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()-overlapMinimumLength+capLength;
	if(m_ed->m_EXTENSION_currentPosition<(int)m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()){
		if(!m_Machine_getPaths_DONE){
			if(m_ed->m_EXTENSION_currentPosition!=position1	&&m_ed->m_EXTENSION_currentPosition!=position2){
				m_Machine_getPaths_DONE=true;
				m_Machine_getPaths_result.clear();// avoids major leak... LOL
			}else{
				int position=m_ed->m_EXTENSION_currentPosition;
				GraphPath*path=&(m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i]);

				Kmer kmerObjectAtPosition;
				path->at(position,&kmerObjectAtPosition);

				getPaths(kmerObjectAtPosition);
			}
		}else{
			// at this point, we have the paths that has the said vertex in them.
			// remove selfId.
			vector<Direction> a;
			for(int i=0;i<(int)m_Machine_getPaths_result.size();i++){
				if(m_Machine_getPaths_result[i].getWave()!=currentId){
					a.push_back(m_Machine_getPaths_result[i]);
				}
			}
			m_FINISH_pathsForPosition->push_back(a);
			m_FINISH_coverages.push_back(m_seedingData->m_SEEDING_receivedVertexCoverage);
			if(m_ed->m_EXTENSION_currentPosition==0){
				
				if(m_debugFusionCode){
					cout<<"Trying to join path "<<m_ed->m_EXTENSION_identifiers[m_seedingData->m_SEEDING_i]<<" (";
					cout<<m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()<<" vertices) with something else."<<endl;
				}

				if(m_seedingData->m_SEEDING_i%10==0){
					printf("Rank %i is finishing fusions [%i/%i]\n",getRank(),(int)m_seedingData->m_SEEDING_i+1,(int)m_ed->m_EXTENSION_contigs.size());
	
					if(m_parameters->showMemoryUsage()){
						showMemoryUsage(getRank());
						showDate();
					}
				}
				GraphPath aPath;
				aPath.setKmerLength(m_parameters->getWordSize());

				m_FINISH_newFusions.push_back(aPath);

// TODO: GraphPath provides a way to store coverage too !
				vector<CoverageDepth> b;
				m_FINISH_coverages.clear();
				m_FINISH_vertex_requested=false;
				m_FUSION_eliminated.insert(currentId);
				m_FUSION_pathLengthRequested=false;
				m_checkedValidity=false;
			}

			int position=m_ed->m_EXTENSION_currentPosition;
			Kmer vertex;
			m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].at(position,&vertex);

			m_FINISH_newFusions[m_FINISH_newFusions.size()-1].push_back(&vertex);

			m_Machine_getPaths_DONE=false;
			m_Machine_getPaths_INITIALIZED=false;
			m_Machine_getPaths_result.clear();
			m_ed->m_EXTENSION_currentPosition++;
		}
	}else if(!m_checkedValidity){

		done=true;
		vector<Direction> directions1=(*m_FINISH_pathsForPosition)[position1];
		vector<Direction> directions2=(*m_FINISH_pathsForPosition)[position2];

		// no hits are possible.
		if(directions1.size()==0 || directions2.size()==0 || m_parameters->hasOption("-disable-path-merger")){
			m_checkedValidity=true;
			if(m_debugFusionCode){
				cout<<"no hit found at all."<<endl;
			}
		}else{

		// basically, directions1 contains the paths at a particular vertex in the path
		// directions2 contains the paths at another vertex in the path
		// both vertices are distanced by overlapMinimumLength, or so
		// basically, here we say we have a hit if and only if
		// there is a pair x,y with x in directions1 ad y in directions2
		// with the property that the difference of progressions are exactly overlapMinimumLength (progressions
		// are simply positions of these vertices on another path.)
		// 

			int hits=0;
			map<PathHandle,vector<int> > indexOnDirection2;

			set<PathHandle> in1;
			
			for(int j=0;j<(int)directions1.size();j++){
				PathHandle waveId=directions1[j].getWave();
				in1.insert(waveId);
			}

			// index the index for each wave
			for(int j=0;j<(int)directions2.size();j++){
				PathHandle waveId=directions2[j].getWave();
				if(in1.count(waveId)==0){
					continue;
				}
				if(indexOnDirection2.count(waveId)==0){
					vector<int> emptyVector;
					indexOnDirection2[waveId]=emptyVector;
				}
				indexOnDirection2[waveId].push_back(j);
			}
	
			// find all hits
			//
			for(int i=0;i<(int)directions1.size();i++){
				PathHandle wave1=directions1[i].getWave();
				if(indexOnDirection2.count(wave1)==0){
					continue;
				}
				vector<int> searchResults=indexOnDirection2[wave1];
				int progression1=directions1[i].getProgression();
				for(int j=0;j<(int)searchResults.size();j++){
					int index2=searchResults[j];
					int otherProgression=directions2[index2].getProgression();
					int observedDistance=(progression1-otherProgression+1);
					int expectedDistance=(overlapMinimumLength-2*capLength);
					
					if(m_debugFusionCode)
						cout<<"Rank "<<m_parameters->getRank()<<" selfDistance: "<<expectedDistance<<" otherDistance: "<<observedDistance<<endl;

					if(observedDistance==expectedDistance){
						// this is 
						done=false;
						hits++;
						m_selectedPath=wave1;
						m_selectedPosition=progression1+capLength;
					}
				}
			}

			indexOnDirection2.clear();
	
			/**
 	*		if there is more than one hit, they must be repeated regions. (?)
 	*
 	*/
			if(hits>1){// we don't support that right now.
				if(m_debugFusionCode)
					cout<<"More than one hit, "<<hits<<" hits found."<<endl;
				done=true;
			}else if(hits==1){
				if(m_debugFusionCode)
					cout<<"Exactly 1 hit found!"<<endl;
			}


			m_checkedValidity=true;
		}
	}else if(!m_mappingConfirmed){
		if(position1<=m_validationPosition && m_validationPosition<=position2){
			if(!m_Machine_getPaths_DONE){
				#ifdef CONFIG_ASSERT
				assert(m_seedingData->m_SEEDING_i<m_ed->m_EXTENSION_contigs.size());
				assert(m_ed->m_EXTENSION_currentPosition<(int)m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size());
				#endif

				int position=m_ed->m_EXTENSION_currentPosition;
				GraphPath*path=&(m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i]);
				Kmer kmerObject;
				path->at(position,&kmerObject);

				getPaths(kmerObject);
			}else{

				bool found=false;
				for(int i=0;i<(int)m_Machine_getPaths_result.size();i++){
					if(m_Machine_getPaths_result[i].getWave()==m_selectedPath){
						found=true;
						break;
					}
				}
				if(!found){
					if(m_debugFusionCode){
						cout<<"Fallback to staged path, selection is not confirmed."<<endl;
						cout<<" validationPosition= "<<m_validationPosition<<endl;
					}

					done=true;// the selection is not confirmed
				}else{
					m_validationPosition++;// added
					m_Machine_getPaths_DONE=false;
					m_Machine_getPaths_INITIALIZED=false;
				}
			}
		}else if(m_validationPosition>position2){
			if(m_debugFusionCode){
				cout<<"Safely confirmed mapping for path "<<m_ed->m_EXTENSION_identifiers[m_seedingData->m_SEEDING_i]<<endl;
				cout<<" hit is path "<<m_selectedPath<<" at position "<<m_selectedPosition<<endl;
			}
			m_mappingConfirmed=true;

		}else{
			m_validationPosition++;
			m_Machine_getPaths_DONE=false;
			m_Machine_getPaths_INITIALIZED=false;
		}
	}else{
		// check if it is there for at least overlapMinimumLength
		PathHandle pathId=m_selectedPath;
		int progression=m_selectedPosition;

		// only one path, just go where it goes...
		// except if it has the same number of vertices and
		// the same start and end.
		if(m_FINISH_pathLengths.count(pathId)==0){
			if(!m_FUSION_pathLengthRequested){
				Rank rankId=getRankFromPathUniqueId(pathId);
				MessageUnit*message=(MessageUnit*)m_outboxAllocator->allocate(sizeof(MessageUnit));
				message[0]=pathId.getValue();
	
				#ifdef CONFIG_ASSERT
				assert(rankId<m_size);
				#endif
	
				Message aMessage(message,1,rankId,RAY_MPI_TAG_GET_PATH_LENGTH,getRank());
				m_outbox->push_back(&aMessage);
				m_FUSION_pathLengthRequested=true;
				m_FUSION_pathLengthReceived=false;
			}else if(m_FUSION_pathLengthReceived){
				if(m_debugFusionCode)
					cout<<"caching length for path object "<<pathId<<", value is "<<m_FUSION_receivedLength<<endl;
				m_FINISH_pathLengths[pathId]=m_FUSION_receivedLength;
			}
		}else if(m_FINISH_pathLengths[pathId]!=0 // 0 means the path does not exist.
		&&m_FINISH_pathLengths[pathId]!=(int)m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()){// avoid fusion of same length.
			int nextPosition=progression+1;
			if(nextPosition<m_FINISH_pathLengths[pathId]){
				// get the vertex
				// get its paths,
				// and continue...
				if(!m_FINISH_vertex_requested){
					Rank rankId=getRankFromPathUniqueId(pathId);
					MessageUnit*message=(MessageUnit*)m_outboxAllocator->allocate(sizeof(MessageUnit)*2);
					message[0]=pathId.getValue();
					message[1]=nextPosition;
					Message aMessage(message,2,rankId,RAY_MPI_TAG_GET_PATH_VERTEX,getRank());
					m_outbox->push_back(&aMessage);
					m_FINISH_vertex_requested=true;
					m_FINISH_vertex_received=false;

				}else if(m_FINISH_vertex_received){
					m_FINISH_newFusions[m_FINISH_newFusions.size()-1].push_back(&m_FINISH_received_vertex);
					m_FINISH_vertex_requested=false;
					m_selectedPosition++;
					m_FINISH_fusionOccured=true;
				}
			}else{
				#ifdef SHOW_FUSION
				cout<<"Rank "<<getRank()<<": extension-"<<m_ed->m_EXTENSION_identifiers[m_seedingData->m_SEEDING_i]<<" ("<<m_ed->m_EXTENSION_contigs[m_seedingData->m_SEEDING_i].size()<<" vertices) and extension-"<<pathId<<" ("<<m_FINISH_pathLengths[pathId]<<" vertices) make a fusion, result: "<<m_FINISH_newFusions[m_FINISH_newFusions.size()-1].size()<<" vertices."<<endl;
				#endif

				done=true;
				if(m_parameters->showMemoryUsage()){
					showMemoryUsage(getRank());
				}
			}
		}else{
			done=true;
		}
	}
	if(done){
		// there is nothing we can do.
		m_seedingData->m_SEEDING_i++;
		m_FINISH_vertex_requested=false;
		m_ed->m_EXTENSION_currentPosition=0;
		m_FUSION_pathLengthRequested=false;
		m_Machine_getPaths_INITIALIZED=false;
		m_Machine_getPaths_DONE=false;
		m_checkedValidity=false;
		delete m_FINISH_pathsForPosition;
		m_FINISH_pathsForPosition=new vector<vector<Direction> >;
		m_FINISH_coverages.clear();
		m_mappingConfirmed=false;
		m_validationPosition=0;
	}
}

/*
 * get the Directions taken by a vertex.
 *
 * m_Machine_getPaths_INITIALIZED must be set to false before any calls.
 * also, you must set m_Machine_getPaths_DONE to false;
 *
 * when done, m_Machine_getPaths_DONE is true
 * and
 * the result is in m_Machine_getPaths_result (a vector<Direction>)
 */
void FusionData::getPaths(Kmer vertex){
	if(!m_Machine_getPaths_INITIALIZED){
		m_Machine_getPaths_INITIALIZED=true;
		m_FUSION_paths_requested=false;
		m_Machine_getPaths_DONE=false;
		m_Machine_getPaths_result.clear();
		return;
	}
	if(m_cacheForRepeatedVertices.find(vertex,false)!=NULL){
		SplayNode<Kmer ,Direction*>*node=m_cacheForRepeatedVertices.find(vertex,false);
		#ifdef CONFIG_ASSERT
		assert(node!=NULL);
		#endif
		Direction**ddirect=node->getValue();
		#ifdef CONFIG_ASSERT
		assert(ddirect!=NULL);
		#endif
		Direction*d=*ddirect;
		while(d!=NULL){
			m_Machine_getPaths_result.push_back(*d);
			d=d->getNext();
		}
		m_Machine_getPaths_DONE=true;
	}else if(!m_FUSION_paths_requested){
		MessageUnit*message=(MessageUnit*)m_outboxAllocator->allocate(2*sizeof(MessageUnit));
		int bufferPosition=0;
		vertex.pack(message,&bufferPosition);
		message[bufferPosition++]=0;
		Message aMessage(message,bufferPosition,
			m_parameters->vertexRank(&vertex),RAY_MPI_TAG_ASK_VERTEX_PATHS,getRank());
		m_outbox->push_back(&aMessage);
		m_FUSION_paths_requested=true;
		m_FUSION_paths_received=false;
		m_FUSION_receivedPaths.clear();
	}else if(m_FUSION_paths_received){
		#ifdef CONFIG_ASSERT
		for(int i=0;i<(int)m_FUSION_receivedPaths.size();i++){
			assert(getRankFromPathUniqueId(m_FUSION_receivedPaths[i].getWave())<m_size);
		}
		#endif
		// save the result in the cache.
		#ifdef CONFIG_ASSERT
		assert(m_cacheForRepeatedVertices.find(vertex,false)==NULL);
		#endif

		bool inserted;
		SplayNode<Kmer ,Direction*>*node=m_cacheForRepeatedVertices.insert(vertex,&m_cacheAllocator,&inserted);
		int i=0;
		Direction*theDirection=NULL;
		while(i<(int)m_Machine_getPaths_result.size()){
			Direction*newDirection=(Direction*)m_cacheAllocator.allocate(sizeof(Direction)*1);
			*newDirection=m_Machine_getPaths_result[i];
			newDirection->setNext(theDirection);
			theDirection=newDirection;
			i++;
		}

		Direction**ddirect=node->getValue();
		*ddirect=theDirection;

		#ifdef CONFIG_ASSERT
		if(m_Machine_getPaths_result.size()==0){
			assert(*(m_cacheForRepeatedVertices.find(vertex,false)->getValue())==NULL);
		}
		#endif

		m_Machine_getPaths_DONE=true;
	}
}

void FusionData::initialise(){
	m_FUSION_direct_fusionDone=false;
	m_FUSION_first_done=false;
	m_FUSION_paths_requested=false;
}

void FusionData::registerPlugin(ComputeCore*core){

	PluginHandle plugin=core->allocatePluginHandle();

	m_plugin=plugin;

	core->setPluginName(plugin,"FusionData");
	core->setPluginDescription(plugin,"This plugin propagates paths");
	core->setPluginAuthors(plugin,"Sébastien Boisvert");
	core->setPluginLicense(plugin,"GNU General Public License version 3");

	RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS=core->allocateSlaveModeHandle(plugin);
	core->setSlaveModeObjectHandler(plugin,RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS, __GetAdapter(FusionData,RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS));
	core->setSlaveModeSymbol(plugin,RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS,"RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS");
}

void FusionData::resolveSymbols(ComputeCore*core){
	RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS=core->getSlaveModeFromSymbol(m_plugin,"RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS");
	RAY_SLAVE_MODE_DO_NOTHING=core->getSlaveModeFromSymbol(m_plugin,"RAY_SLAVE_MODE_DO_NOTHING");

	RAY_MPI_TAG_ASK_VERTEX_PATHS=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_ASK_VERTEX_PATHS");
	RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED");
	RAY_MPI_TAG_FINISH_FUSIONS_FINISHED=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_FINISH_FUSIONS_FINISHED");
	RAY_MPI_TAG_GET_PATH_LENGTH=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_GET_PATH_LENGTH");
	RAY_MPI_TAG_GET_PATH_VERTEX=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_GET_PATH_VERTEX");
	RAY_MPI_TAG_SAVE_WAVE_PROGRESSION_WITH_REPLY=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_SAVE_WAVE_PROGRESSION_WITH_REPLY");

	__BindPlugin(FusionData);

	__BindAdapter(FusionData,RAY_SLAVE_MODE_DISTRIBUTE_FUSIONS);
}
