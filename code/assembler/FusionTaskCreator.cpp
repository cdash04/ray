/*
 	Ray
    Copyright (C)  2011, 2012  Sébastien Boisvert

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

#include <assembler/FusionTaskCreator.h>
#include <assembler/FusionWorker.h>
#include <core/OperatingSystem.h>

void FusionTaskCreator::call_RAY_SLAVE_MODE_FUSION(){

	mainLoop();
}

void FusionTaskCreator::constructor(VirtualProcessor*virtualProcessor,StaticVector*outbox,
		RingAllocator*outboxAllocator,int*mode,Parameters*parameters,
		vector<vector<Kmer> >*paths,vector<uint64_t>*pathIdentifiers,
		set<uint64_t>*eliminated,VirtualCommunicator*virtualCommunicator){
	m_virtualCommunicator=virtualCommunicator;

	m_eliminated=eliminated;
	m_paths=paths;
	m_pathIdentifiers=pathIdentifiers;

	m_outboxAllocator=outboxAllocator;
	m_outbox=outbox;
	m_slaveMode=mode;
	m_parameters=parameters;

	/* for TaskCreator */
	m_initialized=false;
	m_virtualProcessor=virtualProcessor;
}

/** initialize the whole thing */
void FusionTaskCreator::initializeMethod(){
	/* cout<<"FusionTaskCreator::initializeMethod()"<<endl; */
	m_iterator=0;
	m_currentWorkerIdentifier=0;
	m_reverseStrand=false;
}

/** finalize the whole thing */
void FusionTaskCreator::finalizeMethod(){
	/** all the paths */
	int numberOfPaths=m_paths->size();

	/** the number of eliminated paths */
	int eliminatedPaths=m_eliminated->size();

	/** make sure this number is at le ast all the rreverse paths */

	bool removedPaths=false;
	
	if(eliminatedPaths>= 1)
		removedPaths = true;


	cout<<"Rank "<<m_parameters->getRank()<<" FusionTaskCreator ["<<m_completedJobs<<"/"<<2*m_paths->size()<<"]"<<endl;
	cout<<"Statistics: all paths: "<<numberOfPaths<<" eliminated: "<<eliminatedPaths<<endl;

	/* send a message */
	uint64_t*message=(uint64_t*)m_outboxAllocator->allocate(sizeof(uint64_t));
	message[0]=removedPaths;
	Message aMessage(message,1,MASTER_RANK,RAY_MPI_TAG_FUSION_DONE,m_parameters->getRank());
	m_outbox->push_back(aMessage);

	/* set the mode */
	(*m_slaveMode)=RAY_SLAVE_MODE_DO_NOTHING;

	if(m_parameters->showMemoryUsage()){
		showMemoryUsage(m_parameters->getRank());
	}

	m_initialized=false;
}

/** has an unassigned task left to compute */
bool FusionTaskCreator::hasUnassignedTask(){
	return m_iterator < (uint64_t)m_paths->size();
}

/** assign the next task to a worker and return this worker 
 *
 * \author Sébastien Boisvert
 *
 * Code reviews
 *
 * 2011-08-30 -- Code review by Élénie Godzaridis (found bug with new/malloc)
 */
Worker*FusionTaskCreator::assignNextTask(){

	if(m_currentWorkerIdentifier % 10== 0){
		cout<<"Rank "<<m_parameters->getRank()<<" FusionTaskCreator assignNextTask ["<<m_currentWorkerIdentifier<<"/"<<m_paths->size()*2<<"]"<<endl;

		if(m_parameters->showMemoryUsage()){
			showMemoryUsage(m_parameters->getRank());
		}
	}

	FusionWorker*worker=new FusionWorker;
	worker->constructor(m_currentWorkerIdentifier,&(m_paths->at(m_iterator)),m_pathIdentifiers->at(m_iterator),m_reverseStrand,m_virtualCommunicator,m_parameters,m_outboxAllocator,

	RAY_MPI_TAG_ASK_VERTEX_PATH,
	RAY_MPI_TAG_ASK_VERTEX_PATHS_SIZE,
	RAY_MPI_TAG_GET_PATH_LENGTH
);


	m_currentWorkerIdentifier++;
	if(m_reverseStrand){
		m_iterator++;
		m_reverseStrand=false;
	}else{
		m_reverseStrand=true;
	}

	return worker;
}

/** get the result of a worker */
void FusionTaskCreator::processWorkerResult(Worker*worker){

	if(m_completedJobs % 10== 0){
		cout<<"Rank "<<m_parameters->getRank()<<" FusionTaskCreator processWorkerResult ["<<m_completedJobs<<"/"<<m_paths->size()*2<<"]"<<endl;

		if(m_parameters->showMemoryUsage()){
			showMemoryUsage(m_parameters->getRank());
		}
	}

	FusionWorker*worker2=(FusionWorker*)worker;

	if(worker2->isPathEliminated()){
		if(m_parameters->hasOption("-debug-fusions"))
			cout<<"eliminated !"<<endl;
		m_eliminated->insert(worker2->getPathIdentifier());
	}else{
		if(m_parameters->hasOption("-debug-fusions"))
			cout<<"kept !"<<endl;
	}
}

/** destroy a worker */
void FusionTaskCreator::destroyWorker(Worker*worker){
	delete worker;
}

void FusionTaskCreator::registerPlugin(ComputeCore*core){
	PluginHandle plugin=core->allocatePluginHandle();

	m_plugin=plugin;

	core->setPluginName(plugin,"FusionTaskCreator");
	core->setPluginDescription(plugin,"Contained paths are dumped");
	core->setPluginAuthors(plugin,"Sébastien Boisvert");
	core->setPluginLicense(plugin,"GNU General Public License version 3");

	RAY_SLAVE_MODE_FUSION=core->allocateSlaveModeHandle(plugin);
	m_adapter_RAY_SLAVE_MODE_FUSION.setObject(this);
	core->setSlaveModeObjectHandler(plugin,RAY_SLAVE_MODE_FUSION, &m_adapter_RAY_SLAVE_MODE_FUSION);
	core->setSlaveModeSymbol(plugin,RAY_SLAVE_MODE_FUSION,"RAY_SLAVE_MODE_FUSION");

	RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED_REPLY_REPLY=core->allocateMessageTagHandle(plugin);
	core->setMessageTagSymbol(plugin,RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED_REPLY_REPLY,"RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED_REPLY_REPLY");

}

void FusionTaskCreator::resolveSymbols(ComputeCore*core){
	RAY_SLAVE_MODE_FUSION=core->getSlaveModeFromSymbol(m_plugin,"RAY_SLAVE_MODE_FUSION");
	RAY_SLAVE_MODE_DO_NOTHING=core->getSlaveModeFromSymbol(m_plugin,"RAY_SLAVE_MODE_DO_NOTHING");

	RAY_MPI_TAG_ASK_VERTEX_PATH=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_ASK_VERTEX_PATH");
	RAY_MPI_TAG_ASK_VERTEX_PATHS_SIZE=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_ASK_VERTEX_PATHS_SIZE");
	RAY_MPI_TAG_GET_PATH_LENGTH=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_GET_PATH_LENGTH");
	RAY_MPI_TAG_FUSION_DONE=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_FUSION_DONE");
	RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED_REPLY_REPLY=core->getMessageTagFromSymbol(m_plugin,"RAY_MPI_TAG_DISTRIBUTE_FUSIONS_FINISHED_REPLY_REPLY");
}
