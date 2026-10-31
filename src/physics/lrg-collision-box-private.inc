/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Continuous moving AABB/triangle SAT, shared by every box-sweep consumer.
 * The existing box query supplies transformed triangles and BVH pruning. */
typedef struct {
	gdouble center[3],extent[3],motion[3];
	gdouble best,normal[3],point[3];
	LrgCollisionTri triangle;
	gboolean found;
} BoxSweep;

/* Intersect one separating-axis time interval with the running interval.
 * Axes are normalized so initial penetration depths are comparable. */
static gboolean
box_sweep_axis(const BoxSweep *query, const LrgCollisionTri *tri,
	const gdouble input[3], gdouble *enter, gdouble *leave,
	gdouble entering[3], gdouble *depth, gdouble initial[3])
{
	gdouble axis[3],length,lo=G_MAXDOUBLE,hi=-G_MAXDOUBLE,r=0,v=0,a,b,sign;
	guint i,j;
	length=v3_length(input);
	if (length<1e-30) return TRUE;
	for (i=0;i<3;i++) { axis[i]=input[i]/length; r+=fabs(axis[i])*query->extent[i]; v+=axis[i]*query->motion[i]; }
	for (j=0;j<3;j++) {
		gdouble projection=0;
		for (i=0;i<3;i++) projection+=(tri->v[3*j+i]-query->center[i])*axis[i];
		lo=MIN(lo,projection); hi=MAX(hi,projection);
	}
	lo-=r; hi+=r;
	/* A coplanar zero-width projection is not a separating constraint for
	 * a flat/line box moving in that same affine plane. */
	if (lo==0 && hi==0 && v==0) return TRUE;
	/* A convex interval cannot be re-entered during linear motion after
	 * starting on its outward/tangent boundary. This also permits sliding. */
	if ((lo==0 && v<=0) || (hi==0 && v>=0)) return FALSE;
	if (lo<=0 && hi>=0) {
		gdouble penetration=MIN(-lo,hi);
		if (penetration<*depth) {
			*depth=penetration; sign=-lo<=hi ? -1 : 1;
			for (i=0;i<3;i++) initial[i]=sign*axis[i];
		}
	}
	if (v==0) return lo<=0 && hi>=0;
	a=lo/v; b=hi/v; sign=v>0 ? -1 : 1;
	if (a>b) { gdouble swap=a; a=b; b=swap; }
	if (a>*enter) { *enter=a; for (i=0;i<3;i++) entering[i]=sign*axis[i]; }
	*leave=MIN(*leave,b);
	return *enter<=*leave;
}

/* Clip the triangle to the contact AABB. The centroid of the retained
 * polygon lies on the actual triangle and in the closed box. At most nine
 * vertices survive six planes, with fixed scratch capacity and no heap. */
static void
box_sweep_contact(const BoxSweep *query, const LrgCollisionTri *tri,
	gdouble time, gdouble point[3])
{
	gdouble vertices[2][16][3],center[3];
	guint count=3,side=0,plane,i,k;
	memcpy(vertices[0],tri->v,9*sizeof(gdouble));
	for (k=0;k<3;k++) center[k]=query->center[k]+time*query->motion[k];
	for (plane=0;plane<6 && count;plane++) {
		guint axis=plane/2,next=0,other=1-side;
		gdouble sign=plane%2 ? -1 : 1;
		gdouble bound=center[axis]+sign*query->extent[axis];
		for (i=0;i<count;i++) {
			guint previous=(i+count-1)%count;
			gdouble a=sign*(bound-vertices[side][previous][axis]);
			gdouble b=sign*(bound-vertices[side][i][axis]);
			gdouble tolerance=64*DBL_EPSILON*MAX(1.0,MAX(fabs(bound),query->extent[axis]));
			gboolean inside_a=a>=-tolerance,inside_b=b>=-tolerance;
			if (inside_a!=inside_b && next<16) {
				gdouble fraction=CLAMP(a/(a-b),0,1);
				for (k=0;k<3;k++) vertices[other][next][k]=vertices[side][previous][k]+fraction*(vertices[side][i][k]-vertices[side][previous][k]);
				next++;
			}
			if (inside_b && next<16) { memcpy(vertices[other][next],vertices[side][i],3*sizeof(gdouble)); next++; }
		}
		count=next; side=other;
	}
	for (k=0;k<3;k++) {
		point[k]=0;
		for (i=0;i<count;i++) point[k]+=vertices[side][i][k]/count;
	}
	/* Only numerical cancellation at a grazing contact can empty the clip.
	 * The closest point remains on the real triangle, never a box proxy. */
	if (!count) { gdouble ignored[3]; lrg_collision_segment_triangle(center,center,tri->v,ignored,point); }
}

static gboolean
box_sweep_visit(const LrgCollisionTri *tri, gpointer data)
{
	BoxSweep *query=data;
	gdouble enter=-G_MAXDOUBLE,leave=1,entering[3]={0},initial[3]={0},depth=G_MAXDOUBLE;
	gdouble edges[3][3],axis[3],time;
	guint i,j,k;
	for (i=0;i<3;i++) for (k=0;k<3;k++) edges[i][k]=tri->v[3*((i+1)%3)+k]-tri->v[3*i+k];
	for (i=0;i<3;i++) {
		memset(axis,0,sizeof(axis)); axis[i]=1;
		if (!box_sweep_axis(query,tri,axis,&enter,&leave,entering,&depth,initial)) return TRUE;
	}
	v3_cross(edges[0],edges[1],axis);
	if (!box_sweep_axis(query,tri,axis,&enter,&leave,entering,&depth,initial)) return TRUE;
	for (i=0;i<3;i++) for (j=0;j<3;j++) {
		gdouble basis[3]={0}; basis[j]=1; v3_cross(edges[i],basis,axis);
		if (!box_sweep_axis(query,tri,axis,&enter,&leave,entering,&depth,initial)) return TRUE;
	}
	if (leave<0 || enter>1) return TRUE;
	time=MAX(0,enter);
	if (query->found && time>=query->best) return TRUE;
	query->found=TRUE; query->best=time; query->triangle=*tri;
	memcpy(query->normal,enter<0 ? initial : entering,sizeof(query->normal));
	box_sweep_contact(query,tri,time,query->point);
	return TRUE;
}

gboolean
lrg_collision_mesh_sweep_box(LrgCollisionMesh *self, const gfloat *center,
	const gfloat *half_extents, const gfloat *motion, LrgCollisionHit *out_hit,
	gdouble *out_fraction, GError **error)
{
	BoxSweep query;
	gdouble min[3],max[3],length;
	guint i;
	gboolean point=TRUE;
	g_return_val_if_fail(LRG_IS_COLLISION_MESH(self),FALSE);
	if (!center || !half_extents || !motion) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Box sweep requires center, half extents and motion"); return FALSE; }
	memset(&query,0,sizeof(query));
	for (i=0;i<3;i++) {
		if (!isfinite(center[i]) || !isfinite(half_extents[i]) || !isfinite(motion[i]) || half_extents[i]<0 || fabs(center[i])>1e12 || half_extents[i]>1e12 || fabs(motion[i])>1e12) {
			g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Box sweep coordinates/extents must be finite, nonnegative and bounded"); return FALSE;
		}
		query.center[i]=center[i]; query.extent[i]=half_extents[i]; query.motion[i]=motion[i];
		if (half_extents[i]!=0) point=FALSE;
		min[i]=MIN(query.center[i],query.center[i]+query.motion[i])-query.extent[i];
		max[i]=MAX(query.center[i],query.center[i]+query.motion[i])+query.extent[i];
	}
	length=v3_length(query.motion);
	if (point) {
		LrgCollisionHit hit;
		if (!length || !lrg_collision_mesh_raycast(self,center,motion,(gfloat)length,&hit)) return FALSE;
		if (out_hit) *out_hit=hit;
		if (out_fraction) *out_fraction=CLAMP(hit.distance/length,0,1);
		return TRUE;
	}
	lrg_collision_mesh_query_box(self,min,max,box_sweep_visit,&query);
	if (!query.found) return FALSE;
	if (out_hit) hit_store(out_hit,query.point,query.normal,query.best*length,query.triangle.triangle,query.triangle.tag,query.triangle.instance_id);
	if (out_fraction) *out_fraction=query.best;
	return TRUE;
}
