/*! \file mpigadgetio.cxx
 *  \brief this file contains routines used with MPI compilation and gadget io and domain construction.
 */

#ifdef USEMPI

//-- For MPI

#include "stf.h"

#include "gadgetitems.h"
#include "endianutils.h"

/// \name Gadget Domain decomposition
//@{

/*!
    Determine the domain decomposition.\n
    Here the domains are constructured in data units
    only ThisTask==0 should call this routine. It is tricky to get appropriate load balancing and correct number of particles per processor.\n

    I could use recursive binary splitting like kd-tree along most spread axis till have appropriate number of volumes corresponding
    to number of processors.

    NOTE: assume that cannot store data so position information is read Nsplit times to determine boundaries of subvolumes
    could also randomly subsample system and produce tree from that
    should store for each processor the node structure generated by the domain decomposition
    what I could do is read file twice, one to get extent and other to calculate entropy then decompose
    along some primary axis, then choose orthogonal axis, keep iterating till have appropriate number of subvolumes
    then store the boundaries of the subvolume. this means I don't store data but get at least reasonable domain decomposition

    NOTE: pkdgrav uses orthoganl recursive bisection along with kd-tree, gadget-2 uses peno-hilbert curve to map particles and oct-trees
    the question with either method is guaranteeing load balancing. for ORB achieved by splitting (sub)volume along a dimension (say one with largest spread or max entropy)
    such that either side of the cut has approximately the same number of particles (ie: median splitting). But for both cases, load balancing requires particle information
    so I must load the system then move particles about to ensure load balancing.

    Main thing first is get the dimensional extent of the system.
    then I could get initial splitting just using mid point between boundaries along each dimension.
    once have that initial splitting just load data then start shifting data around.
*/
void MPIDomainExtentGadget(Options &opt){
    #define SKIP2 Fgad[i].read((char*)&dummy, sizeof(dummy));
    Int_t i,j,k,n,m,temp,pc,pc_new, Ntot;
    int dummy;
    FLOAT ctemp[3];
    char   buf[200];
    char DATA[5];
    fstream *Fgad;
    struct gadget_header *header;

    if (ThisTask==0) {
    Fgad=new fstream[opt.num_files];
    header=new gadget_header[opt.num_files];
    for(i=0; i<opt.num_files; i++)
    {
        if(opt.num_files>1) sprintf(buf,"%s.%d",opt.fname,int(i));
        else sprintf(buf,"%s",opt.fname);
        Fgad[i].open(buf,ios::in);
        if(!Fgad[i]) {
            cout<<"can't open file "<<buf<<endl;
            exit(0);
        }
        else cout<<"reading "<<buf<<endl;
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        Fgad[i].read((char*)&header[i], sizeof(gadget_header));
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        //endian indep call
        header[i].Endian();
    }
    for (m=0;m<3;m++) {mpi_xlim[m][0]=0;mpi_xlim[m][1]=header[0].BoxSize;}
    /*
    for (m=0;m<3;m++) {mpi_xlim[m][0]=MAXVALUE;mpi_xlim[m][1]=-MAXVALUE;}
    cout<<"Getting domain extent"<<endl;
    for(i=0;i<opt.num_files; i++)
    {
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
        fprintf(stderr,"reading... %s\n",DATA);
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        for(k=0;k<6;k++)
        {
            for(n=0;n<header[i].npart[k];n++)
            {
                Fgad[i].read((char*)&ctemp[0], sizeof(FLOAT)*3);
                for (m=0;m<3;m++) ctemp[m]=LittleFloat(ctemp[m]);
                if (opt.partsearchtype==PSTALL)
                    for (m=0;m<3;m++) {if (ctemp[m]<mpi_xlim[m][0]) mpi_xlim[m][0]=ctemp[m];if (ctemp[m]>mpi_xlim[m][1]) mpi_xlim[m][1]=ctemp[m];}
                else if (opt.partsearchtype==PSTDARK)
                    if (!k==GASTYPE||k==STARTYPE)
                        for (m=0;m<3;m++) {if (ctemp[m]<mpi_xlim[m][0]) mpi_xlim[m][0]=ctemp[m];if (ctemp[m]>mpi_xlim[m][1]) mpi_xlim[m][1]=ctemp[m];}
                else if (opt.partsearchtype==PSTSTAR)
                    if (k==STARTYPE)
                        for (m=0;m<3;m++) {if (ctemp[m]<mpi_xlim[m][0]) mpi_xlim[m][0]=ctemp[m];if (ctemp[m]>mpi_xlim[m][1]) mpi_xlim[m][1]=ctemp[m];}
                else if (opt.partsearchtype==PSTGAS)
                    if (k==GASTYPE)
                        for (m=0;m<3;m++) {if (ctemp[m]<mpi_xlim[m][0]) mpi_xlim[m][0]=ctemp[m];if (ctemp[m]>mpi_xlim[m][1]) mpi_xlim[m][1]=ctemp[m];}
            }
        }
    }
    */
    //There may be issues with particles exactly on the edge of a domain so before expanded limits by a small amount
    //now only done if a specific compile option passed
#ifdef MPIEXPANDLIM
    for (int j=0;j<3;j++) {
        Double_t dx=0.001*(mpi_xlim[j][1]-mpi_xlim[j][0]);
        mpi_xlim[j][0]-=dx;mpi_xlim[j][1]+=dx;
    }
#endif
    }

    //make sure limits have been found
    MPI_Barrier(MPI_COMM_WORLD);
    if (NProcs==1) {
        for (i=0;i<3;i++) {
            mpi_domain[ThisTask].bnd[i][0]=mpi_xlim[i][0];
            mpi_domain[ThisTask].bnd[i][1]=mpi_xlim[i][1];
        }
    }
}

///to update the decomposition based on gadget information
void MPIDomainDecompositionGadget(Options &opt){
    #define SKIP2 Fgad[i].read((char*)&dummy, sizeof(dummy));
    Int_t i,j,k,n,m,temp,pc,pc_new, Ntot;
    int Nsplit,isplit;
    Int_t nbins1d,nbins3d, ibin[3];
    Double_t **histo1d;//for initial projection and estimate of domain decomposition
    //Double_t ***histo3d,**histo2d;//finer scale histo for more accurate decomposition
    Double_t *histo3d,*histo2d;//finer scale histo for more accurate decomposition
    Coordinate *histo3dbinval;
    Coordinate2D *histo2dbinval;
    int dummy;
    FLOAT ctemp[3];
    char   buf[200];
    char DATA[5];
    fstream *Fgad;
    struct gadget_header *header;
    Double_t a;
    int b;
    if (ThisTask==0) {
        /*
    cout<<"domain decomposition"<<endl;
    //then read the data to calculate the mean, variance, limits, and fill
    //1d histo with uniform bin spacing
    Fgad=new fstream[opt.num_files];
    header=new gadget_header[opt.num_files];
    for (i=0; i<opt.num_files; i++)
    {
        if(opt.num_files>1) sprintf(buf,"%s.%d",opt.fname,i);
        else sprintf(buf,"%s",opt.fname);
        Fgad[i].open(buf,ios::in);
        if(!Fgad[i]) {
            cout<<"can't open file "<<buf<<endl;
            exit(0);
        }
        else cout<<"reading "<<buf<<endl;
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
        fprintf(stderr,"reading... %s\n",DATA);
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        Fgad[i].read((char*)&header[i], sizeof(gadget_header));
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        //endian indep call
        header[i].Endian();
    }
    //read in data to calcualte variance in each dimension
    double mean[3],var[3];
    for (j=0;j<3;j++)mean[j]=var[j]=0.;
    Ntot=0;
    for (i=0;i<opt.num_files; i++)
        for (j=0;j<6;j++) {
            if (opt.partsearchtype==PSTALL) Ntot+=header[i].npart[j];
            else if (opt.partsearchtype==PSTDARK)
                if (!j==GASTYPE||j==STARTYPE) Ntot+=header[i].npart[j];
            else if (opt.partsearchtype==PSTSTAR)
                if (j==STARTYPE) Ntot+=header[i].npart[j];
            else if (opt.partsearchtype==PSTGAS)
                if (j==GASTYPE) Ntot+=header[i].npart[j];
        }
    nbins1d=ceil(log10((Double_t)Ntot)/log10(2.0)+1)*pow(Ntot,1./3.);
    nbins3d=ceil(log10((Double_t)Ntot)/3.0/log10(2.0)+1);
    nbins3d=nbins3d*nbins3d;
    histo1d=new Double_t*[3];
    for (j=0;j<3;j++) histo1d[j]=new Double_t[nbins1d];
    for (j=0;j<3;j++) for (i=0;i<nbins1d;i++) histo1d[j][i]=0;
    for (i=0;i<opt.num_files; i++)
    {
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
        fprintf(stderr,"reading %s",DATA);
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        for (k=0;k<6;k++)
        {
            for (n=0;n<header[i].npart[k];n++)
            {
                Fgad[i].read((char*)&ctemp[0], sizeof(FLOAT)*3);
                for (m=0;m<3;m++) ctemp[m]=LittleFloat(ctemp[m]);
                for (m=0;m<3;m++) ibin[m]=(int)floor((ctemp[m]-mpi_xlim[m][0])/(mpi_xlim[m][1]-mpi_xlim[m][0])*(Double_t)nbins1d);
                if (opt.partsearchtype==PSTALL)
                    for (m=0;m<3;m++) {
                        mean[m]+=ctemp[m];
                        var[m]+=ctemp[m]*ctemp[m];
                        histo1d[m][ibin[m]]++;
                    }
                else if (opt.partsearchtype==PSTDARK)
                    if (!k==GASTYPE||k==STARTYPE)
                    for (m=0;m<3;m++) {
                        mean[m]+=ctemp[m];
                        var[m]+=ctemp[m]*ctemp[m];
                        histo1d[m][ibin[m]]++;
                    }
                else if (opt.partsearchtype==PSTSTAR)
                    if (k==STARTYPE)
                    for (m=0;m<3;m++) {
                        mean[m]+=ctemp[m];
                        var[m]+=ctemp[m]*ctemp[m];
                        histo1d[m][ibin[m]]++;
                    }
                else if (opt.partsearchtype==PSTGAS)
                    if (k==GASTYPE)
                    for (m=0;m<3;m++) {
                        mean[m]+=ctemp[m];
                        var[m]+=ctemp[m]*ctemp[m];
                        histo1d[m][ibin[m]]++;
                    }
            }
        }
        Fgad[i].close();
    }
    PriorityQueue *pq=new PriorityQueue(3);
    for (j=0;j<3;j++) {
        Double_t e=0.;
        mean[j]*=1.0/(Double_t)Ntot;
        var[j]*=1.0/(Double_t)Ntot;
        var[j]-=mean[j]*mean[j];
        //for (k=0;k<nbins1d;k++) e-=histo1d[j][k]/(Double_t)Ntot*log10((histo1d[j][k]+1.0)/(Double_t)Ntot);
        //e/=log10((Double_t)nbins1d)
        pq->Push(j,1.0/var[j]);
        //pq->Push(j,e);
    }
    for (j=0;j<3;j++) {mpi_ideltax[j]=pq->TopQueue();pq->Pop();}
    delete pq;
    //determine the number of splits in each dimension
    Nsplit=log((float)NProcs)/log(2.0);
    isplit=0;
    for (j=0;j<3;j++) mpi_nxsplit[j]=0;
    for (j=0;j<Nsplit;j++) {
        mpi_nxsplit[mpi_ideltax[isplit++]]++;
        if (isplit==3) isplit=0;
    }
    for (j=0;j<3;j++) mpi_nxsplit[j]=pow(2.0,mpi_nxsplit[j]);

    //construct coarse grain histograms with non-uniform bins
    histo3dbinval=new Coordinate[nbins3d+1];
    histo2dbinval=new Coordinate2D[nbins3d+1];
    Double_t sum,binvalue,nfac,kbnd;
    for (m=0;m<3;m++) {
        j=mpi_ideltax[m];
        sum=0.;
        binvalue=0;
        kbnd=1.0;
        nfac=1.0/(Double_t)nbins3d;
        histo3dbinval[m][0]=mpi_xlim[j][0];
        histo3dbinval[m][nbins3d]=mpi_xlim[j][1];
        if (m<2) {
            histo2dbinval[m][0]=mpi_xlim[j][1];
            histo2dbinval[m][nbins3d]=mpi_xlim[j][1];
        }
        for (i=0;i<nbins1d;i++) {
            sum+=histo1d[j][i];
            binvalue=histo1d[j][i];
            if(sum>=Ntot*nfac*kbnd) {
                //no interpolation
//                double value=mpi_xlim[j][0]+((mpi_xlim[j][1]-mpi_xlim[j][0])/(Double_t)nbins1d)*(i-0.5);
                //linear interpolation on log scale
                double value=mpi_xlim[j][0]+((mpi_xlim[j][1]-mpi_xlim[j][0])/(Double_t)nbins1d)*(i-1)+((mpi_xlim[j][1]-mpi_xlim[j][0])/(Double_t)nbins1d)/(log(histo1d[j][i]))*(log(sum)-log(Ntot*nfac*kbnd));
                histo3dbinval[(int)kbnd][m]=value;
                if (m<2) histo2dbinval[(int)kbnd][m]=value;
                kbnd+=1.0;
            }
        }
    }
    histo3d=new Double_t[nbins3d*nbins3d*nbins3d];
    histo2d=new Double_t[nbins3d*nbins3d];
    Int_t nbins3d3=nbins3d*nbins3d*nbins3d;
    Int_t nbins3d2=nbins3d*nbins3d;
    for (i=0;i<nbins3d3;i++) histo3d[i]=0;
    for (i=0;i<nbins3d2;i++) histo2d[i]=0;
    for (i=0; i<opt.num_files; i++)
    {
        if(opt.num_files>1) sprintf(buf,"%s.%d",opt.fname,i);
        else sprintf(buf,"%s",opt.fname);
        Fgad[i].open(buf,ios::in);
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
        fprintf(stderr,"reading %s",DATA);
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        Fgad[i].read((char*)&header[i], sizeof(gadget_header));
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        //endian indep call
        header[i].Endian();
#ifdef GADGET2FORMAT
        SKIP2;
        Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
        SKIP2;
        SKIP2;
        fprintf(stderr,"reading %s",DATA);
#endif
        Fgad[i].read((char*)&dummy, sizeof(dummy));
        for (k=0;k<6;k++)
        {
            for (n=0;n<header[i].npart[k];n++)
            {
                Fgad[i].read((char*)&ctemp[0], sizeof(FLOAT)*3);
                for (m=0;m<3;m++) ctemp[m]=LittleFloat(ctemp[m]);
                for (m=0;m<3;m++) {
                    for (j=0;j<nbins3d;j++) {
                        if(ctemp[mpi_ideltax[m]]>=histo3dbinval[j][m]&&ctemp[mpi_ideltax[m]]<histo3dbinval[j+1][m]) {
                            ibin[m]=j;
                            break;
                        }
                    }
                }
                if (opt.partsearchtype==PSTALL) {
                    histo3d[ibin[0]*nbins3d2+ibin[1]+nbins3d+ibin[2]]++;
                    histo2d[ibin[0]*nbins3d+ibin[1]]++;
                    histo1d[0][ibin[0]]++;
                }
                else if (opt.partsearchtype==PSTDARK)
                    if (!k==GASTYPE||k==STARTYPE) {
                    histo3d[ibin[0]*nbins3d2+ibin[1]+nbins3d+ibin[2]]++;
                    histo2d[ibin[0]*nbins3d+ibin[1]]++;
                    histo1d[0][ibin[0]]++;
                    }
                else if (opt.partsearchtype==PSTSTAR)
                    if (k==STARTYPE) {
                    histo3d[ibin[0]*nbins3d2+ibin[1]+nbins3d+ibin[2]]++;
                    histo2d[ibin[0]*nbins3d+ibin[1]]++;
                    histo1d[0][ibin[0]]++;
                    }
                else if (opt.partsearchtype==PSTGAS)
                    if (k==GASTYPE) {
                    histo3d[ibin[0]*nbins3d2+ibin[1]+nbins3d+ibin[2]]++;
                    histo2d[ibin[0]*nbins3d+ibin[1]]++;
                    histo1d[0][ibin[0]]++;
                    }
            }
        }
    }
    //determine mpi domains using histograms to determine boundaries.
    //for all the cells along the boundary of axis with the third split axis (smallest variance)
    //set the domain limits to the sims limits
    int ix=mpi_ideltax[0],iy=mpi_ideltax[1],iz=mpi_ideltax[2];
    int mpitasknum;
    for (j=0;j<mpi_nxsplit[iy];j++) {
        for (i=0;i<mpi_nxsplit[ix];i++) {
            mpitasknum=i+j*mpi_nxsplit[ix]+0*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[iz][0]=mpi_xlim[iz][0];
            mpitasknum=i+j*mpi_nxsplit[ix]+(mpi_nxsplit[iz]-1)*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[iz][1]=mpi_xlim[iz][1];
        }
    }
    //here for domains along second axis
    for (k=0;k<mpi_nxsplit[iz];k++) {
        for (i=0;i<mpi_nxsplit[ix];i++) {
            mpitasknum=i+0*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[iy][0]=mpi_xlim[iy][0];
            mpitasknum=i+(mpi_nxsplit[iy]-1)*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[iy][1]=mpi_xlim[iy][1];
        }
    }
    //finally along axis with largest variance
    for (k=0;k<mpi_nxsplit[iz];k++) {
        for (j=0;j<mpi_nxsplit[iy];j++) {
            mpitasknum=0+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[ix][0]=mpi_xlim[ix][0];
            mpitasknum=(mpi_nxsplit[ix]-1)+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
            mpi_domain[mpitasknum].bnd[ix][1]=mpi_xlim[ix][1];
        }
    }
    //here use the three different histograms to define the boundary
    int start[3],end[3];
    Double_t bndval[3],binsum[3],lastbin;
    start[0]=start[1]=start[2]=0;
    for (i=0;i<mpi_nxsplit[ix];i++) {
        binsum[0]=0;
        //first determine boundary for primary split axis
        for (int n1=start[0];n1<nbins3d-1;n1++){
            binsum[0]+=histo1d[0][n1];
            if (binsum[0]<Ntot/(Double_t)mpi_nxsplit[ix]&&binsum[0]+histo1d[0][n1+1]>=Ntot/(Double_t)mpi_nxsplit[ix]) {
                end[0]=n1;
                //bndval[0]=histo3dbinval[n1][0];
                //interpolate boundary value
                //bndval[0]=histo3dbinval[n1][0]+
                //    (Ntot/(Double_t)mpi_nxsplit[ix]-binsum[0])*(histo3dbinval[n1+1][0]-histo3dbinval[n1][0])/(histo1d[0][n1+1]);
                bndval[0]=histo3dbinval[n1][0]+
                    (log(Ntot/(Double_t)mpi_nxsplit[ix])-log(binsum[0]))*(histo3dbinval[n1+1][0]-histo3dbinval[n1][0])/(log(histo1d[0][n1+1]));
                break;
            }
        }
        if(i<mpi_nxsplit[ix]-1) {
        for (j=0;j<mpi_nxsplit[iy];j++) {
            for (k=0;k<mpi_nxsplit[iz];k++) {
                //define upper limit
                mpitasknum=i+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[ix][1]=bndval[0];
                //define lower limit
                mpitasknum=(i+1)+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[ix][0]=bndval[0];
            }
        }
        }
        //now for secondary splitting
        ///\todo must alter so that add first column then do check since at the moment, n2,n3 start at zero and
        ///thus cannot interpolate back. need to check beginning, and end points
        if (mpi_nxsplit[iy]>1)
        for (j=0;j<mpi_nxsplit[iy];j++) {
            binsum[1]=0;
            for (int n2=start[1];n2<nbins3d;n2++){
                //lastbin=0; for (int n1=start[0];n1<end[0];n1++) lastbin+=histo2d[n1][n2];
                lastbin=0; for (int n1=start[0];n1<end[0];n1++) lastbin+=histo2d[n1*nbins3d+n2];
                binsum[1]+=lastbin;
                if (binsum[1]>=Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy])&&binsum[1]-lastbin<Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy])){
                    end[1]=n2;
                    //bndval[1]=histo3dbinval[n2][1];
                    //bndval[1]=histo3dbinval[n2][1]+
                    //(Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy])-binsum[1])*(histo3dbinval[n2][1]-histo3dbinval[n2-1][1])/(lastbin);
                    bndval[1]=histo3dbinval[n2][1]+
                    (log(Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy]))-log(binsum[1]))*(histo3dbinval[n2][1]-histo3dbinval[n2-1][1])/(log(lastbin));
                    break;
                }
            }
            if(j<mpi_nxsplit[iy]-1) {
            for (k=0;k<mpi_nxsplit[iz];k++) {
                mpitasknum=i+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[iy][1]=bndval[1];
                mpitasknum=i+(j+1)*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[iy][0]=bndval[1];
            }
            }
            if (mpi_nxsplit[iz]>1)
            for (k=0;k<mpi_nxsplit[iz];k++) {
                binsum[2]=0;
                for (int n3=start[2];n3<nbins3d;n3++){
                    //lastbin=0; for (int n1=start[0];n1<end[0];n1++)for (int n2=start[1];n2<end[1];n2++) lastbin+=histo3d[n1][n2][n3];
                    lastbin=0; for (int n1=start[0];n1<end[0];n1++)for (int n2=start[1];n2<end[1];n2++) lastbin+=histo3d[n1*nbins3d2+n2*nbins3d+n3];
                    binsum[2]+=lastbin;
                    if (binsum[2]>=Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy]*mpi_nxsplit[iz])&&binsum[2]-lastbin<Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy]*mpi_nxsplit[iz])){
                        end[2]=n3;
                        //bndval[2]=histo3dbinval[n3][2];
                        //bndval[2]=histo3dbinval[n3][2]+
                        //(log(Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy]*mpi_nxsplit[iz]))-log(binsum[2]))*(histo3dbinval[n3][2]-histo3dbinval[n3-1][2])/(log(lastbin));
                        bndval[2]=histo3dbinval[n3][2]+
                        (Ntot/(Double_t)(mpi_nxsplit[ix]*mpi_nxsplit[iy]*mpi_nxsplit[iz])-binsum[2])*(histo3dbinval[n3][2]-histo3dbinval[n3-1][2])/(lastbin);
                        break;
                    }
                }
                if (k<mpi_nxsplit[iz]-1){
                mpitasknum=i+j*mpi_nxsplit[ix]+k*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[iz][1]=bndval[2];
                mpitasknum=i+j*mpi_nxsplit[ix]+(k+1)*(mpi_nxsplit[ix]*mpi_nxsplit[iy]);
                mpi_domain[mpitasknum].bnd[iz][0]=bndval[2];
                }
                start[2]=end[2]+1;
                binsum[2]=0;
            }
            start[1]=end[1]+1;
            binsum[1]=0;
        }
        start[0]=end[0]+1;
        binsum[0]=0;
    }
    delete[] histo2d;
    delete[] histo3d;
    for (j=0;j<3;j++) delete[] histo1d[j];
    delete[] histo1d;
    delete[] histo3dbinval;
    delete[] histo2dbinval;
    */
    }
}

///reads a gadget file to determine number of particles in each MPIDomain
void MPINumInDomainGadget(Options &opt)
{
    #define SKIP2 Fgad[i].read((char*)&dummy, sizeof(dummy));
    InitEndian();
    if (NProcs>1) {
    MPIDomainExtentGadget(opt);
    MPIInitialDomainDecomposition(opt);
    MPIDomainDecompositionGadget(opt);
    Int_t i,j,k,n,m,temp,pc,pc_new, Ntot,indark,ingas,instar;
    Int_t idval;
    Int_t ntot_withmasses;
    int dummy;
    double z,aadjust,Hubble;
    FLOAT ctemp[3], dtemp;
    char   buf[200];
    char DATA[5];
    fstream *Fgad;
    struct gadget_header *header;
    Int_t Nlocalold=Nlocal;
    int *ireadtask,*readtaskID;
    ireadtask=new int[NProcs];
    readtaskID=new int[opt.nsnapread];
    std::vector<int> ireadfile(opt.num_files);
    MPIDistributeReadTasks(opt,ireadtask,readtaskID);

    MPI_Status status;
    Int_t Nlocalbuf,ibuf=0,*Nbuf, *Nbaryonbuf;
    Nbuf=new Int_t[NProcs];
    Nbaryonbuf=new Int_t[NProcs];
    for (int j=0;j<NProcs;j++) Nbuf[j]=0;
    for (int j=0;j<NProcs;j++) Nbaryonbuf[j]=0;

    //opening file
    Fgad=new fstream[opt.num_files];
    header=new gadget_header[opt.num_files];
    if (ireadtask[ThisTask]>=0) {
        MPISetFilesRead(opt,ireadfile,ireadtask);
        for(i=0; i<opt.num_files; i++) if(ireadfile[i])
        {
            if(opt.num_files>1) sprintf(buf,"%s.%d",opt.fname,int(i));
            else sprintf(buf,"%s",opt.fname);
            Fgad[i].open(buf,ios::in);
        #ifdef GADGET2FORMAT
            SKIP2;
            Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
            SKIP2;
            SKIP2;
            fprintf(stderr,"reading... %s\n",DATA);
        #endif
            Fgad[i].read((char*)&dummy, sizeof(dummy));
            Fgad[i].read((char*)&header[i], sizeof(gadget_header));
            Fgad[i].read((char*)&dummy, sizeof(dummy));
            //endian indep call
            header[i].Endian();

        #ifdef GADGET2FORMAT
            Fgad[i].read((char*)&dummy, sizeof(dummy));
            Fgad[i].read((char*)&DATA[0],sizeof(char)*4);DATA[4] = '\0';
            Fgad[i].read((char*)&dummy, sizeof(dummy));
            Fgad[i].read((char*)&dummy, sizeof(dummy));
        #endif
            Fgad[i].read((char*)&dummy, sizeof(dummy));
            for(k=0;k<NGTYPE;k++)
            {
                for(n=0;n<header[i].npart[k];n++)
                {
                    Fgad[i].read((char*)&ctemp[0], sizeof(FLOAT)*3);
                    ibuf=MPIGetParticlesProcessor(opt, ctemp[0],ctemp[1],ctemp[2]);
                    if (opt.partsearchtype==PSTALL) {
                        Nbuf[ibuf]++;
                    }
                    else if (opt.partsearchtype==PSTDARK) {
                        if (!(k==GGASTYPE||k==GSTARTYPE||k==GBHTYPE)) {
                            Nbuf[ibuf]++;
                        }
                        else {
                            if (opt.iBaryonSearch) {
                                Nbaryonbuf[ibuf]++;
                            }
                        }
                    }
                    else if (opt.partsearchtype==PSTSTAR) {
                        if (k==STARTYPE) {
                            Nbuf[ibuf]++;
                        }
                    }
                    else if (opt.partsearchtype==PSTGAS) {
                        if (k==GASTYPE) {
                            Nbuf[ibuf]++;
                        }
                    }
                }
            }
            Fgad[i].close();
        }
    }
    //now having read number of particles, run all gather
    Int_t mpi_nlocal[NProcs];
    MPI_Allreduce(Nbuf,mpi_nlocal,NProcs,MPI_Int_t,MPI_SUM,MPI_COMM_WORLD);
    Nlocal=mpi_nlocal[ThisTask];
    if (opt.iBaryonSearch) {
        MPI_Allreduce(Nbaryonbuf,mpi_nlocal,NProcs,MPI_Int_t,MPI_SUM,MPI_COMM_WORLD);
        Nlocalbaryon[0]=mpi_nlocal[ThisTask];
    }
    }
}

//@}

#endif
